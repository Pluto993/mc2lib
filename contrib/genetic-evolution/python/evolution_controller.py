"""
Evolution Controller - 遗传算法演化主控制器（完整版本）

负责管理整个演化过程:
1. 加载上一代状态
2. 协调测试生成、运行、评估
3. 执行遗传算法迭代
4. 保存状态并继续下一代
"""

import json
import os
import sys
import time
import subprocess
import logging
from pathlib import Path
from typing import List, Dict, Any, Optional, Set, Tuple
from dataclasses import dataclass, asdict
from datetime import datetime

# 导入我们的模块
from genetic_algorithm import GeneticAlgorithm, TestGenome
from gem5_runner import GEM5Runner, TestResult
from fitness_evaluator import FitnessEvaluator

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
    handlers=[
        logging.FileHandler('logs/evolution.log'),
        logging.StreamHandler()
    ]
)
logger = logging.getLogger(__name__)


@dataclass
class EvolutionState:
    """演化状态"""
    generation: int
    population_size: int
    best_fitness: float
    avg_fitness: float
    fitaddrs: Set[Tuple[int, int]]  # 发现的有趣地址对
    tests: List[TestGenome]
    start_time: str
    last_update: str
    total_tests_run: int = 0
    
    def to_dict(self) -> Dict:
        """转换为字典（用于 JSON 序列化）"""
        return {
            'generation': self.generation,
            'population_size': self.population_size,
            'best_fitness': self.best_fitness,
            'avg_fitness': self.avg_fitness,
            'fitaddrs': [list(p) for p in self.fitaddrs],  # set → list
            'tests': [asdict(t) for t in self.tests],
            'start_time': self.start_time,
            'last_update': self.last_update,
            'total_tests_run': self.total_tests_run
        }
    
    @classmethod
    def from_dict(cls, data: Dict) -> 'EvolutionState':
        """从字典加载"""
        tests = [TestGenome(**t) for t in data.get('tests', [])]
        # 将 fitaddrs 从 list 转换为 set of tuples
        fitaddrs_list = data.get('fitaddrs', [])
        fitaddrs = {tuple(p) for p in fitaddrs_list}
        
        return cls(
            generation=data['generation'],
            population_size=data['population_size'],
            best_fitness=data['best_fitness'],
            avg_fitness=data['avg_fitness'],
            fitaddrs=fitaddrs,
            tests=tests,
            start_time=data['start_time'],
            last_update=data['last_update'],
            total_tests_run=data.get('total_tests_run', 0)
        )


class EvolutionController:
    """演化控制器"""
    
    def __init__(self, config_path: str = 'config/evolution.yaml'):
        self.config = self._load_config(config_path)
        self.state: Optional[EvolutionState] = None
        self.work_dir = Path(self.config.get('work_dir', '.'))
        self.work_dir.mkdir(parents=True, exist_ok=True)
        
        # 初始化遗传算法引擎
        self.genetic_algorithm = GeneticAlgorithm(self.config)
        
        # 初始化 gem5 运行器
        self.gem5_runner = GEM5Runner(self.config)
        
        # 初始化适应度评估器
        self.fitness_evaluator = FitnessEvaluator(self.config)
        
    def _load_config(self, config_path: str) -> Dict:
        """加载配置（暂时用默认值）"""
        # TODO: 实现 YAML 加载
        return {
            'work_dir': '/root/.openclaw/workspace/mc2lib-evolution',
            'population_size': 20,  # 减小以加快测试
            'max_generations': 100,
            'mutation_rate': 0.1,
            'elite_ratio': 0.1,
            'parallel_jobs': 4,
            'gem5_timeout': 60,
            'min_threads': 2,
            'max_threads': 2,
            'min_ops_per_thread': 5,  # 减少操作数
            'max_ops_per_thread': 10,
            'P_USEL': 0.2,
            'P_BFA': 0.05,
            
            # gem5 配置
            'gem5_binary': '/root/.openclaw/workspace/gem5/build/RISCV/gem5.opt',
            'gem5_config': '/root/.openclaw/workspace/gem5/configs/mc2lib_multicore_simple.py',
            'gem5_root': '/root/.openclaw/workspace/gem5',
            'num_cpus': 2,
            
            # 适应度配置
            'total_possible_pairs': 100,
            'new_pair_weight': 0.1,
            'violation_weight': 0.2
        }
    
    def load_state(self, generation: int = -1) -> EvolutionState:
        """加载演化状态"""
        if generation == -1:
            # 查找最新的状态
            state_files = sorted(Path('logs').glob('evolution_state_gen*.json'))
            if state_files:
                state_path = state_files[-1]
                logger.info(f"Loading state from {state_path}")
                with open(state_path, 'r') as f:
                    return EvolutionState.from_dict(json.load(f))
        else:
            state_path = Path(f'logs/evolution_state_gen{generation}.json')
            if state_path.exists():
                logger.info(f"Loading state from {state_path}")
                with open(state_path, 'r') as f:
                    return EvolutionState.from_dict(json.load(f))
        
        # 创建初始状态
        logger.info("Creating initial state (Generation 0)")
        return EvolutionState(
            generation=0,
            population_size=self.config['population_size'],
            best_fitness=0.0,
            avg_fitness=0.0,
            fitaddrs=set(),
            tests=[],
            start_time=datetime.now().isoformat(),
            last_update=datetime.now().isoformat()
        )
    
    def save_state(self, state: EvolutionState):
        """保存演化状态"""
        state.last_update = datetime.now().isoformat()
        state_path = Path(f'logs/evolution_state_gen{state.generation}.json')
        state_path.parent.mkdir(parents=True, exist_ok=True)
        
        with open(state_path, 'w') as f:
            json.dump(state.to_dict(), f, indent=2)
        
        # 同时保存为 latest
        latest_path = Path('logs/evolution_state_latest.json')
        with open(latest_path, 'w') as f:
            json.dump(state.to_dict(), f, indent=2)
        
        logger.info(f"Saved state to {state_path}")
    
    def run_generation(self, state: EvolutionState) -> EvolutionState:
        """运行一代演化"""
        gen = state.generation
        logger.info(f"=" * 60)
        logger.info(f"Starting Generation {gen}")
        logger.info(f"=" * 60)
        
        # 如果是第 0 代，生成初始种群
        if gen == 0 and not state.tests:
            state.tests = self.genetic_algorithm.generate_initial_population(seed=42)
        
        # 1. 生成测试（如果是新一代）
        if gen > 0:
            logger.info(f"[{gen}] Evolving population via genetic operators...")
            state.tests = self.genetic_algorithm.evolve(state.tests, state.fitaddrs)
        
        # 2. 编译测试
        logger.info(f"[{gen}] Compiling tests...")
        self._compile_tests(state)
        
        # 3. 在 gem5 上运行测试
        logger.info(f"[{gen}] Running tests on gem5...")
        self._run_tests_on_gem5(state)
        
        # 4. 评估适应度
        logger.info(f"[{gen}] Evaluating fitness...")
        self._evaluate_fitness(state)
        
        # 5. 更新统计信息
        self._update_statistics(state)
        
        # 6. 保存状态
        self.save_state(state)
        
        logger.info(f"Generation {gen} complete!")
        logger.info(f"  Best fitness: {state.best_fitness:.4f}")
        logger.info(f"  Avg fitness:  {state.avg_fitness:.4f}")
        logger.info(f"  Fitaddrs:     {len(state.fitaddrs)}")
        logger.info(f"=" * 60)
        
        return state
    
    def _compile_tests(self, state: EvolutionState):
        """编译所有测试"""
        gen_dir = self.work_dir / f'tests/generation_{state.generation}'
        gen_dir.mkdir(parents=True, exist_ok=True)
        
        compiled_count = 0
        failed_count = 0
        
        for test in state.tests:
            # 生成 C 代码
            c_code = self.genetic_algorithm.genome_to_c_code(test)
            c_file = gen_dir / f'{test.test_id}.c'
            with open(c_file, 'w') as f:
                f.write(c_code)
            
            # 编译
            binary_file = gen_dir / test.test_id
            compile_cmd = [
                'riscv64-linux-gnu-gcc',
                '-std=c11', '-O2', '-static',
                '-o', str(binary_file),
                str(c_file)
            ]
            
            try:
                result = subprocess.run(compile_cmd, capture_output=True, text=True, timeout=30)
                if result.returncode == 0:
                    compiled_count += 1
                else:
                    failed_count += 1
                    logger.warning(f"Failed to compile {test.test_id}")
            except Exception as e:
                failed_count += 1
                logger.error(f"Compile error for {test.test_id}: {e}")
        
        logger.info(f"  Compiled: {compiled_count}/{len(state.tests)} "
                   f"({failed_count} failed)")
    
    def _run_tests_on_gem5(self, state: EvolutionState):
        """在 gem5 上运行所有测试"""
        gen_dir = self.work_dir / f'tests/generation_{state.generation}'
        
        # 准备测试列表
        tests_to_run = []
        for test in state.tests:
            binary_file = gen_dir / test.test_id
            if binary_file.exists():
                tests_to_run.append((binary_file, test.test_id))
        
        if not tests_to_run:
            logger.error("  No tests to run!")
            return
        
        # 使用 gem5 运行器（并行）
        results = self.gem5_runner.run_parallel(tests_to_run, self.work_dir)
        
        # 将结果关联回测试
        result_map = {r.test_id: r for r in results}
        for test in state.tests:
            if test.test_id in result_map:
                result = result_map[test.test_id]
                test.coverage = {
                    'address_pairs': result.address_pairs,
                    'new_pairs': [],  # 稍后计算
                    'violations': result.violations
                }
        
        state.total_tests_run += len(tests_to_run)
        
        success_count = sum(1 for r in results if r.success)
        logger.info(f"  gem5 runs complete: {success_count}/{len(results)} succeeded")
    
    def _evaluate_fitness(self, state: EvolutionState):
        """评估所有测试的适应度"""
        # 创建 TestResult 对象
        test_results = []
        for test in state.tests:
            result = TestResult(
                test_id=test.test_id,
                success=True,
                timeout=False,
                error_msg=None,
                sim_seconds=1.0,
                events=[],
                address_pairs=test.coverage.get('address_pairs', []),
                violations=test.coverage.get('violations', []),
                gem5_output=""
            )
            test_results.append(result)
        
        # 批量评估
        metrics_list, updated_fitaddrs = self.fitness_evaluator.evaluate_batch(
            test_results, state.fitaddrs
        )
        
        # 更新适应度和 fitaddrs
        for test, metrics in zip(state.tests, metrics_list):
            test.fitness = metrics.final_score
            test.coverage['new_pairs'] = metrics.details['new_pairs']
        
        state.fitaddrs = updated_fitaddrs
    
    def _update_statistics(self, state: EvolutionState):
        """更新统计信息"""
        if state.tests:
            fitnesses = [t.fitness for t in state.tests]
            state.best_fitness = max(fitnesses)
            state.avg_fitness = sum(fitnesses) / len(fitnesses)
    
    def _check_convergence(self, state: EvolutionState) -> bool:
        """检查是否收敛"""
        # TODO: 实现更复杂的收敛检测
        return False
    
    def run(self, start_generation: int = 0, max_generations: Optional[int] = None):
        """运行演化循环"""
        if max_generations is None:
            max_generations = self.config['max_generations']
        
        # 加载或创建初始状态
        if start_generation > 0:
            self.state = self.load_state(start_generation - 1)
        else:
            self.state = self.load_state()
        
        start_gen = self.state.generation
        logger.info(f"Starting evolution from generation {start_gen}")
        
        try:
            for gen in range(start_gen, max_generations):
                self.state.generation = gen
                self.state = self.run_generation(self.state)
                
                # 检查收敛条件
                if self._check_convergence(self.state):
                    logger.info(f"Convergence detected at generation {gen}")
                    break
                
                # 进入下一代
                self.state.generation += 1
        
        except KeyboardInterrupt:
            logger.info("Evolution interrupted by user")
            self.save_state(self.state)
        except Exception as e:
            logger.error(f"Error during evolution: {e}", exc_info=True)
            self.save_state(self.state)
            raise
        
        logger.info("Evolution complete!")
        logger.info(f"Final generation: {self.state.generation}")
        logger.info(f"Best fitness: {self.state.best_fitness:.4f}")
        logger.info(f"Total tests run: {self.state.total_tests_run}")


def main():
    """主函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='McVerSi Genetic Evolution Controller')
    parser.add_argument('--config', default='config/evolution.yaml',
                        help='Configuration file')
    parser.add_argument('--start-gen', type=int, default=0,
                        help='Starting generation (0 for new, >0 to resume)')
    parser.add_argument('--max-gen', type=int, default=None,
                        help='Maximum generations to run')
    parser.add_argument('--work-dir', default='/root/.openclaw/workspace/mc2lib-evolution',
                        help='Working directory')
    
    args = parser.parse_args()
    
    # 切换工作目录
    os.chdir(args.work_dir)
    
    # 创建控制器并运行
    controller = EvolutionController(args.config)
    controller.run(start_generation=args.start_gen, max_generations=args.max_gen)


if __name__ == '__main__':
    main()
