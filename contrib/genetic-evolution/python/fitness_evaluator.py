"""
Fitness Evaluator - 适应度评估器

负责:
1. 计算测试的适应度分数
2. 更新 fitaddr 集合
3. 检测内存一致性违例
4. 分析覆盖率
"""

import logging
from typing import List, Set, Dict, Tuple, Optional
from dataclasses import dataclass
import csv

logger = logging.getLogger(__name__)


@dataclass
class FitnessMetrics:
    """适应度指标"""
    base_coverage: float      # 基础覆盖率 (0-1)
    new_pair_bonus: float     # 新地址对奖励
    violation_bonus: float    # 违例发现奖励
    penalty: float            # 惩罚（超时、错误）
    final_score: float        # 最终分数
    
    details: Dict = None


class FitnessEvaluator:
    """适应度评估器"""
    
    def __init__(self, config: Dict):
        self.config = config
        self.total_possible_pairs = config.get('total_possible_pairs', 1000)
        self.new_pair_weight = config.get('new_pair_weight', 0.1)
        self.violation_weight = config.get('violation_weight', 0.2)
        self.timeout_penalty = config.get('timeout_penalty', 0.5)
        self.error_penalty = config.get('error_penalty', 1.0)
        
        logger.info("FitnessEvaluator initialized")
    
    def evaluate(self, test_result, current_fitaddrs: Set[Tuple[str, str]]) -> FitnessMetrics:
        """评估单个测试的适应度"""
        
        # 基础覆盖率
        num_pairs = len(test_result.address_pairs)
        base_coverage = num_pairs / self.total_possible_pairs
        
        # 新地址对奖励
        new_pairs = []
        for pair in test_result.address_pairs:
            if tuple(pair) not in current_fitaddrs:
                new_pairs.append(pair)
        new_pair_bonus = len(new_pairs) * self.new_pair_weight
        
        # 违例奖励
        violation_bonus = len(test_result.violations) * self.violation_weight
        
        # 惩罚
        penalty = 0.0
        if test_result.timeout:
            penalty = self.timeout_penalty
        elif not test_result.success:
            penalty = self.error_penalty
        
        # 最终分数
        final_score = max(0.0, base_coverage + new_pair_bonus + violation_bonus - penalty)
        
        metrics = FitnessMetrics(
            base_coverage=base_coverage,
            new_pair_bonus=new_pair_bonus,
            violation_bonus=violation_bonus,
            penalty=penalty,
            final_score=final_score,
            details={
                'num_pairs': num_pairs,
                'num_new_pairs': len(new_pairs),
                'num_violations': len(test_result.violations),
                'new_pairs': new_pairs
            }
        )
        
        logger.debug(f"Fitness for {test_result.test_id}: {final_score:.4f} "
                    f"(base={base_coverage:.4f}, new={new_pair_bonus:.4f}, "
                    f"vio={violation_bonus:.4f}, pen={penalty:.4f})")
        
        return metrics
    
    def evaluate_batch(self, test_results: List, 
                      current_fitaddrs: Set[Tuple[str, str]]) -> Tuple[List[FitnessMetrics], Set]:
        """批量评估测试"""
        logger.info(f"Evaluating {len(test_results)} tests")
        
        metrics_list = []
        all_new_pairs = set()
        
        for result in test_results:
            metrics = self.evaluate(result, current_fitaddrs)
            metrics_list.append(metrics)
            
            # 收集所有新地址对
            for pair in metrics.details['new_pairs']:
                all_new_pairs.add(tuple(pair))
        
        # 更新 fitaddrs
        updated_fitaddrs = current_fitaddrs.copy()
        updated_fitaddrs.update(all_new_pairs)
        
        logger.info(f"Evaluation complete: "
                   f"{len(all_new_pairs)} new address pairs discovered, "
                   f"total fitaddrs: {len(updated_fitaddrs)}")
        
        return metrics_list, updated_fitaddrs
    
    def detect_sb_violation(self, events: List) -> bool:
        """检测 Store Buffering 违例"""
        # 按核心分组
        core_events = {}
        for event in events:
            if event.core_id not in core_events:
                core_events[event.core_id] = []
            core_events[event.core_id].append(event)
        
        # 检测 SB 模式
        # Core 0: W(x,1) F R(y)->0
        # Core 1: W(y,1) F R(x)->0
        # 如果两个核心都读到 0，则为 SB 违例
        
        if len(core_events) < 2:
            return False
        
        # TODO: 实现完整的 SB 检测逻辑
        # 需要识别 WRITE → FENCE → READ 模式
        
        return False
    
    def analyze_coverage(self, all_test_results: List) -> Dict:
        """分析整体覆盖情况"""
        all_addresses = set()
        all_pairs = set()
        total_events = 0
        total_violations = 0
        
        for result in all_test_results:
            total_events += len(result.events)
            total_violations += len(result.violations)
            
            for pair in result.address_pairs:
                all_pairs.add(tuple(pair))
            
            for event in result.events:
                if event.event_type in ['READ', 'WRITE']:
                    all_addresses.add(event.address)
        
        coverage = {
            'total_addresses': len(all_addresses),
            'total_pairs': len(all_pairs),
            'total_events': total_events,
            'total_violations': total_violations,
            'coverage_ratio': len(all_pairs) / self.total_possible_pairs if self.total_possible_pairs > 0 else 0
        }
        
        logger.info(f"Coverage analysis: "
                   f"{len(all_addresses)} addresses, "
                   f"{len(all_pairs)} pairs, "
                   f"{coverage['coverage_ratio']*100:.2f}% coverage")
        
        return coverage


def main():
    """测试适应度评估器"""
    logging.basicConfig(level=logging.INFO)
    
    # 模拟测试结果
    from gem5_runner import TestResult, MemoryEvent
    
    test_result = TestResult(
        test_id='test1',
        success=True,
        timeout=False,
        error_msg=None,
        sim_seconds=1.0,
        events=[
            MemoryEvent(1000, 0, 0, 'WRITE', '0x0', 1, 0),
            MemoryEvent(2000, 1, 0, 'READ', '0x40', 0, 1),
            MemoryEvent(3000, 2, 1, 'WRITE', '0x40', 1, 0),
            MemoryEvent(4000, 3, 1, 'READ', '0x0', 0, 1),
        ],
        address_pairs=[('0x0', '0x40')],
        violations=[],
        gem5_output=""
    )
    
    config = {
        'total_possible_pairs': 100,
        'new_pair_weight': 0.1,
        'violation_weight': 0.2
    }
    
    evaluator = FitnessEvaluator(config)
    
    # 第一次评估（所有地址对都是新的）
    current_fitaddrs = set()
    metrics = evaluator.evaluate(test_result, current_fitaddrs)
    
    print("\n" + "=" * 60)
    print("Fitness Evaluation:")
    print("=" * 60)
    print(f"Base coverage: {metrics.base_coverage:.4f}")
    print(f"New pair bonus: {metrics.new_pair_bonus:.4f}")
    print(f"Violation bonus: {metrics.violation_bonus:.4f}")
    print(f"Penalty: {metrics.penalty:.4f}")
    print(f"Final score: {metrics.final_score:.4f}")
    print(f"\nDetails:")
    for key, value in metrics.details.items():
        print(f"  {key}: {value}")


if __name__ == '__main__':
    main()
