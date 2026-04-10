#!/usr/bin/env python3
"""
超大规模测试结果分析工具
"""

import re
import glob
from typing import Dict, List, Tuple, Set
from collections import defaultdict

def parse_massive_log(log_file: str) -> Dict:
    """解析日志文件"""
    events = []
    addresses = set()
    types_count = defaultdict(int)
    
    with open(log_file) as f:
        for line in f:
            if not line.startswith('TRACE:'):
                continue
            
            parts = line.strip()[6:].split(',')
            if len(parts) != 7:
                continue
            
            event = {
                'timestamp': int(parts[0]),
                'seq_id': int(parts[1]),
                'core_id': int(parts[2]),
                'type': parts[3],
                'address': parts[4],
                'value': int(parts[5]),
                'po_index': int(parts[6])
            }
            events.append(event)
            types_count[event['type']] += 1
            
            if event['type'] in ['READ', 'WRITE', 'RMW']:
                addresses.add(event['address'])
    
    # 计算地址对
    addr_list = sorted(addresses)
    pairs = set()
    for i in range(len(addr_list)):
        for j in range(i + 1, len(addr_list)):
            pairs.add((addr_list[i], addr_list[j]))
    
    return {
        'file': log_file,
        'events': len(events),
        'addresses': len(addresses),
        'address_pairs': len(pairs),
        'types': dict(types_count)
    }

def analyze_all_logs(pattern="*_log.txt") -> List[Dict]:
    """分析所有日志"""
    log_files = sorted(glob.glob(pattern))
    
    results = []
    for log_file in log_files:
        print(f"分析: {log_file}...")
        try:
            result = parse_massive_log(log_file)
            results.append(result)
        except Exception as e:
            print(f"  ⚠️  错误: {e}")
    
    return results

def print_summary(results: List[Dict]):
    """打印摘要"""
    if not results:
        print("没有找到日志文件")
        return
    
    print("\n" + "=" * 80)
    print("超大规模测试结果分析")
    print("=" * 80)
    print()
    
    print(f"总测试数: {len(results)}")
    print()
    
    # 汇总统计
    total_events = sum(r['events'] for r in results)
    total_addrs = sum(r['addresses'] for r in results)
    total_pairs = sum(r['address_pairs'] for r in results)
    
    print("汇总统计:")
    print(f"  总事件数: {total_events:,}")
    print(f"  总地址数: {total_addrs:,}")
    print(f"  总地址对: {total_pairs:,}")
    print(f"  平均事件/测试: {total_events / len(results):,.0f}")
    print()
    
    # 详细结果
    print("=" * 80)
    print("详细结果")
    print("=" * 80)
    print()
    
    # 排序：按事件数降序
    results_sorted = sorted(results, key=lambda r: r['events'], reverse=True)
    
    print(f"{'文件':<50} {'事件':>10} {'地址':>8} {'地址对':>8}")
    print("-" * 80)
    
    for r in results_sorted:
        filename = r['file'].split('/')[-1]
        print(f"{filename:<50} {r['events']:>10,} {r['addresses']:>8,} {r['address_pairs']:>8,}")
    
    print()
    
    # 最佳结果
    print("=" * 80)
    print("最佳结果")
    print("=" * 80)
    print()
    
    best_events = max(results, key=lambda r: r['events'])
    best_addrs = max(results, key=lambda r: r['addresses'])
    best_pairs = max(results, key=lambda r: r['address_pairs'])
    
    print(f"最多事件: {best_events['file'].split('/')[-1]}")
    print(f"  事件数: {best_events['events']:,}")
    print(f"  类型分布: {best_events['types']}")
    print()
    
    print(f"最多地址: {best_addrs['file'].split('/')[-1]}")
    print(f"  地址数: {best_addrs['addresses']:,}")
    print(f"  地址对: {best_addrs['address_pairs']:,}")
    print()
    
    print(f"最多地址对: {best_pairs['file'].split('/')[-1]}")
    print(f"  地址对: {best_pairs['address_pairs']:,}")
    print()
    
    # 操作类型分布
    print("=" * 80)
    print("操作类型分布")
    print("=" * 80)
    print()
    
    all_types = defaultdict(int)
    for r in results:
        for type_name, count in r['types'].items():
            all_types[type_name] += count
    
    print(f"{'类型':<15} {'数量':>15} {'百分比':>10}")
    print("-" * 40)
    for type_name in sorted(all_types.keys()):
        count = all_types[type_name]
        percentage = count * 100.0 / total_events
        print(f"{type_name:<15} {count:>15,} {percentage:>9.2f}%")
    
    print()
    print("=" * 80)
    print("✅ 分析完成")
    print("=" * 80)

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='超大规模测试结果分析')
    parser.add_argument('--pattern',
                        default='massive_test_*_log.txt',
                        help='日志文件模式 (默认: massive_test_*_log.txt)')
    
    args = parser.parse_args()
    
    results = analyze_all_logs(args.pattern)
    print_summary(results)

if __name__ == '__main__':
    main()
