"""
gem5 Runner - 在 gem5 上运行测试并收集结果

负责:
1. 调用 gem5 运行编译好的测试
2. 解析输出日志和内存事件
3. 支持并行运行多个 gem5 实例
4. 处理超时和错误
"""

import subprocess
import csv
import json
import time
import os
import logging
from pathlib import Path
from typing import Dict, List, Optional, Tuple
from dataclasses import dataclass
from concurrent.futures import ProcessPoolExecutor, as_completed

logger = logging.getLogger(__name__)


@dataclass
class MemoryEvent:
    """内存事件"""
    timestamp: int
    seq_id: int
    core_id: int
    event_type: str  # READ, WRITE, FENCE
    address: str
    value: int
    po_index: int


@dataclass
class TestResult:
    """测试运行结果"""
    test_id: str
    success: bool
    timeout: bool
    error_msg: Optional[str]
    sim_seconds: float
    events: List[MemoryEvent]
    address_pairs: List[Tuple[str, str]]
    violations: List[str]
    gem5_output: str
    
    def to_dict(self) -> Dict:
        return {
            'test_id': self.test_id,
            'success': self.success,
            'timeout': self.timeout,
            'error_msg': self.error_msg,
            'sim_seconds': self.sim_seconds,
            'num_events': len(self.events),
            'address_pairs': self.address_pairs,
            'violations': self.violations
        }


class GEM5Runner:
    """gem5 运行器"""
    
    def __init__(self, config: Dict):
        self.gem5_binary = config.get('gem5_binary', 
                                      '/root/.openclaw/workspace/gem5/build/RISCV/gem5.opt')
        self.gem5_config = config.get('gem5_config',
                                      '/root/.openclaw/workspace/gem5/configs/mc2lib_multicore_simple.py')
        self.gem5_root = Path(config.get('gem5_root', 
                                        '/root/.openclaw/workspace/gem5'))
        self.timeout = config.get('timeout', 90)
        self.num_cpus = config.get('num_cpus', 2)
        self.cpu_type = config.get('cpu_type', 'TimingSimpleCPU')
        self.parallel_jobs = config.get('parallel_jobs', 4)
        
        logger.info(f"GEM5Runner initialized:")
        logger.info(f"  Binary: {self.gem5_binary}")
        logger.info(f"  Config: {self.gem5_config}")
        logger.info(f"  Timeout: {self.timeout}s")
        logger.info(f"  Parallel jobs: {self.parallel_jobs}")
    
    def run_test(self, test_binary: Path, test_id: str, 
                 work_dir: Optional[Path] = None) -> TestResult:
        """运行单个测试"""
        if work_dir is None:
            work_dir = self.gem5_root
        
        # 创建测试专用目录
        test_dir = work_dir / f'runs/{test_id}'
        test_dir.mkdir(parents=True, exist_ok=True)
        
        logger.info(f"Running test {test_id} in {test_dir}")
        
        # 设置环境变量，让测试程序知道输出路径
        env = os.environ.copy()
        env['TEST_RUN_DIR'] = str(test_dir)
        
        # 构建 gem5 命令
        cmd = [
            str(self.gem5_binary),
            '--outdir', str(test_dir / 'm5out'),
            str(self.gem5_config),
            str(test_binary),
            str(self.num_cpus)
        ]
        
        logger.debug(f"Command: {' '.join(cmd)}")
        
        # 运行 gem5
        start_time = time.time()
        timeout_occurred = False
        error_msg = None
        output = ""
        
        try:
            result = subprocess.run(
                cmd,
                cwd=str(self.gem5_root),
                capture_output=True,
                text=True,
                timeout=self.timeout,
                env=env  # 传递环境变量
            )
            output = result.stdout + "\n" + result.stderr
            success = result.returncode == 0
            
            if not success:
                error_msg = f"gem5 exited with code {result.returncode}"
                logger.warning(f"Test {test_id} failed: {error_msg}")
        
        except subprocess.TimeoutExpired:
            timeout_occurred = True
            success = False
            error_msg = f"Timeout after {self.timeout}s"
            logger.warning(f"Test {test_id} timed out")
        
        except Exception as e:
            success = False
            error_msg = str(e)
            logger.error(f"Test {test_id} error: {e}")
        
        elapsed = time.time() - start_time
        
        # 解析结果
        events = []
        address_pairs = []
        violations = []
        sim_seconds = 0.0
        
        if success:
            try:
                # 尝试从 test_dir 查找日志
                events = self._parse_memory_traces(test_dir)
                
                # 如果没找到，尝试从 gem5 根目录复制
                if not events:
                    logger.debug(f"No traces in {test_dir}, checking gem5 root...")
                    gem5_traces = list(self.gem5_root.glob('memory_trace_core*.csv'))
                    if gem5_traces:
                        for trace_file in gem5_traces:
                            dest_file = test_dir / trace_file.name
                            import shutil
                            shutil.copy(trace_file, dest_file)
                            logger.debug(f"Copied {trace_file} to {dest_file}")
                        
                        # 重新解析
                        events = self._parse_memory_traces(test_dir)
                
                # 提取地址对
                address_pairs = self._extract_address_pairs(events)
                
                # 检测违例
                violations = self._detect_violations(events)
                
                # 提取模拟时间
                sim_seconds = self._extract_sim_time(test_dir / 'm5out/stats.txt')
                
                logger.info(f"Test {test_id} complete: {len(events)} events, "
                          f"{len(address_pairs)} address pairs, "
                          f"{len(violations)} violations")
            
            except Exception as e:
                logger.error(f"Failed to parse results for {test_id}: {e}")
                success = False
                error_msg = f"Parse error: {e}"
        
        return TestResult(
            test_id=test_id,
            success=success,
            timeout=timeout_occurred,
            error_msg=error_msg,
            sim_seconds=sim_seconds,
            events=events,
            address_pairs=address_pairs,
            violations=violations,
            gem5_output=output
        )
    
    def run_parallel(self, tests: List[Tuple[Path, str]], 
                    work_dir: Optional[Path] = None) -> List[TestResult]:
        """并行运行多个测试"""
        logger.info(f"Running {len(tests)} tests in parallel "
                   f"({self.parallel_jobs} workers)")
        
        results = []
        
        with ProcessPoolExecutor(max_workers=self.parallel_jobs) as executor:
            # 提交所有任务
            futures = {
                executor.submit(self.run_test, binary, test_id, work_dir): test_id
                for binary, test_id in tests
            }
            
            # 收集结果
            for future in as_completed(futures):
                test_id = futures[future]
                try:
                    result = future.result()
                    results.append(result)
                    logger.info(f"Test {test_id} finished: "
                              f"{'SUCCESS' if result.success else 'FAILED'}")
                except Exception as e:
                    logger.error(f"Test {test_id} exception: {e}")
                    results.append(TestResult(
                        test_id=test_id,
                        success=False,
                        timeout=False,
                        error_msg=str(e),
                        sim_seconds=0.0,
                        events=[],
                        address_pairs=[],
                        violations=[],
                        gem5_output=""
                    ))
        
        logger.info(f"Parallel execution complete: "
                   f"{sum(1 for r in results if r.success)}/{len(results)} succeeded")
        
        return results
    
    def _parse_memory_traces(self, test_dir: Path) -> List[MemoryEvent]:
        """解析内存事件日志"""
        events = []
        
        # 查找所有核心的日志文件
        trace_files = sorted(test_dir.glob('memory_trace_core*.csv'))
        
        if not trace_files:
            logger.warning(f"No memory trace files found in {test_dir}")
            return events
        
        for trace_file in trace_files:
            try:
                with open(trace_file, 'r') as f:
                    reader = csv.DictReader(f)
                    for row in reader:
                        event = MemoryEvent(
                            timestamp=int(row['timestamp']),
                            seq_id=int(row['seq_id']),
                            core_id=int(row['core_id']),
                            event_type=row['type'],
                            address=row['address'],
                            value=int(row['value']),
                            po_index=int(row['po_index'])
                        )
                        events.append(event)
            except Exception as e:
                logger.error(f"Failed to parse {trace_file}: {e}")
        
        # 按时间戳排序
        events.sort(key=lambda e: e.timestamp)
        
        return events
    
    def _extract_address_pairs(self, events: List[MemoryEvent]) -> List[Tuple[str, str]]:
        """提取访问的地址对"""
        addresses = set()
        
        # 收集所有被访问的地址
        for event in events:
            if event.event_type in ['READ', 'WRITE']:
                addresses.add(event.address)
        
        # 生成所有地址对（排序，避免重复）
        addr_list = sorted(addresses)
        pairs = []
        
        for i in range(len(addr_list)):
            for j in range(i + 1, len(addr_list)):
                pairs.append((addr_list[i], addr_list[j]))
        
        return pairs
    
    def _detect_violations(self, events: List[MemoryEvent]) -> List[str]:
        """检测内存一致性违例（简化版）"""
        violations = []
        
        # TODO: 实现完整的一致性检查
        # 现在只检测简单的 Store Buffering 模式
        
        # 按核心分组事件
        core_events = {}
        for event in events:
            if event.core_id not in core_events:
                core_events[event.core_id] = []
            core_events[event.core_id].append(event)
        
        # 简单的 SB 检测（占位符）
        # 真正的检测应该使用 consistency_checker.py
        
        return violations
    
    def _extract_sim_time(self, stats_file: Path) -> float:
        """从 gem5 统计文件中提取模拟时间"""
        if not stats_file.exists():
            return 0.0
        
        try:
            with open(stats_file, 'r') as f:
                for line in f:
                    if line.startswith('simSeconds'):
                        # simSeconds                                   0.013782
                        parts = line.split()
                        if len(parts) >= 2:
                            return float(parts[1])
        except Exception as e:
            logger.error(f"Failed to parse sim time from {stats_file}: {e}")
        
        return 0.0


def main():
    """测试 gem5 运行器"""
    import sys
    
    logging.basicConfig(level=logging.INFO)
    
    if len(sys.argv) < 2:
        print("Usage: python gem5_runner.py <test_binary>")
        sys.exit(1)
    
    test_binary = Path(sys.argv[1])
    
    config = {
        'gem5_binary': '/root/.openclaw/workspace/gem5/build/RISCV/gem5.opt',
        'gem5_config': '/root/.openclaw/workspace/gem5/configs/mc2lib_multicore_simple.py',
        'gem5_root': '/root/.openclaw/workspace/gem5',
        'timeout': 90,
        'num_cpus': 2,
        'parallel_jobs': 1
    }
    
    runner = GEM5Runner(config)
    result = runner.run_test(test_binary, 'test_manual')
    
    print("\n" + "=" * 60)
    print("Test Result:")
    print("=" * 60)
    print(json.dumps(result.to_dict(), indent=2))
    
    if result.success:
        print(f"\n✅ Test succeeded!")
        print(f"  Events: {len(result.events)}")
        print(f"  Address pairs: {len(result.address_pairs)}")
        print(f"  Violations: {len(result.violations)}")
        print(f"  Sim time: {result.sim_seconds:.6f}s")
    else:
        print(f"\n❌ Test failed: {result.error_msg}")


if __name__ == '__main__':
    main()
