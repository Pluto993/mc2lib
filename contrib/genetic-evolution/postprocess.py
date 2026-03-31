#!/usr/bin/env python3
"""
手动后处理脚本 - 收集 gem5 日志并重新计算适应度
"""

import sys
import json
import shutil
from pathlib import Path

sys.path.insert(0, '/root/.openclaw/workspace/mc2lib-evolution/python')

from gem5_runner import MemoryEvent
from fitness_evaluator import FitnessEvaluator
import csv

# 加载最新状态
with open('logs/evolution_state_gen1.json') as f:
    state = json.load(f)

print("=" * 60)
print("手动后处理 - 收集日志并重新计算适应度")
print("=" * 60)
print()

# gem5 根目录
gem5_root = Path('/root/.openclaw/workspace/gem5')

# 收集所有测试的日志
tests = state['tests']
collected_count = 0

for test in tests:
    test_id = test['test_id']
    test_dir = Path(f'runs/{test_id}')
    
    # 从 gem5 根目录复制日志
    gem5_traces = list(gem5_root.glob('memory_trace_core*.csv'))
    if gem5_traces:
        test_dir.mkdir(parents=True, exist_ok=True)
        for trace_file in gem5_traces:
            dest = test_dir / trace_file.name
            shutil.copy(trace_file, dest)
        collected_count += 1
        print(f"✓ {test_id}: 收集了 {len(gem5_traces)} 个日志文件")

print()
print(f"收集完成: {collected_count}/{len(tests)} 个测试")
print()

# 重新解析日志并计算适应度
print("重新计算适应度...")
print()

evaluator = FitnessEvaluator({
    'total_possible_pairs': 100,
    'new_pair_weight': 0.1,
    'violation_weight': 0.2
})

fitaddrs = set()

for test in tests:
    test_id = test['test_id']
    test_dir = Path(f'runs/{test_id}')
    
    # 解析日志
    events = []
    for trace_file in sorted(test_dir.glob('memory_trace_core*.csv')):
        try:
            with open(trace_file) as f:
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
            print(f"  ✗ {test_id}: 解析错误 - {e}")
            continue
    
    # 提取地址对
    addresses = set()
    for event in events:
        if event.event_type in ['READ', 'WRITE']:
            addresses.add(event.address)
    
    addr_list = sorted(addresses)
    pairs = []
    for i in range(len(addr_list)):
        for j in range(i + 1, len(addr_list)):
            pairs.append((addr_list[i], addr_list[j]))
    
    # 计算新地址对
    new_pairs = []
    for pair in pairs:
        if tuple(pair) not in fitaddrs:
            new_pairs.append(pair)
            fitaddrs.add(tuple(pair))
    
    # 计算适应度
    fitness = len(pairs) / 100.0 + len(new_pairs) * 0.1
    
    test['fitness'] = fitness
    test['coverage'] = {
        'address_pairs': pairs,
        'new_pairs': new_pairs,
        'violations': []
    }
    
    if events:
        print(f"✓ {test_id}: {len(events)} events, {len(pairs)} pairs, "
              f"{len(new_pairs)} new, fitness={fitness:.4f}")

print()
print("=" * 60)
print("重新计算结果:")
print("=" * 60)

# 更新统计
fitnesses = [t['fitness'] for t in tests]
state['best_fitness'] = max(fitnesses)
state['avg_fitness'] = sum(fitnesses) / len(fitnesses)
state['fitaddrs'] = [list(p) for p in fitaddrs]

print(f"Best Fitness: {state['best_fitness']:.4f}")
print(f"Avg Fitness:  {state['avg_fitness']:.4f}")
print(f"Fitaddrs:     {len(state['fitaddrs'])}")
print()

# 找到最佳测试
best_test = max(tests, key=lambda t: t['fitness'])
print(f"最佳测试: {best_test['test_id']}")
print(f"  Fitness: {best_test['fitness']:.4f}")
print(f"  Generation: {best_test['generation']}")
print(f"  Address Pairs: {len(best_test['coverage']['address_pairs'])}")
print(f"  New Pairs: {len(best_test['coverage']['new_pairs'])}")
print()

# 保存更新后的状态
with open('logs/evolution_state_gen1_fixed.json', 'w') as f:
    json.dump(state, f, indent=2)

print("✓ 保存到 logs/evolution_state_gen1_fixed.json")
print("=" * 60)
