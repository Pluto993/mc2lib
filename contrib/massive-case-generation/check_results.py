#!/usr/bin/env python3
"""
增强版结果检查脚本
提供更详细的统计和一致性检查
"""

import re
import glob
from typing import Dict, List
from collections import defaultdict, Counter

def parse_log_detailed(log_file: str) -> Dict:
    """详细解析日志文件"""
    events = []
    addresses = set()
    types_count = defaultdict(int)
    cores = set()
    
    # 地址访问模式
    addr_access_pattern = defaultdict(lambda: {'read': 0, 'write': 0, 'rmw': 0})
    
    # 时间戳分析
    timestamps = []
    
    with open(log_file) as f:
        for line in f:
            if not line.startswith('TRACE:'):
                continue
            
            parts = line.strip()[6:].split(',')
            if len(parts) != 7:
                continue
            
            timestamp = int(parts[0])
            seq_id = int(parts[1])
            core_id = int(parts[2])
            op_type = parts[3]
            address = parts[4]
            value = int(parts[5])
            po_index = int(parts[6])
            
            events.append({
                'timestamp': timestamp,
                'seq_id': seq_id,
                'core_id': core_id,
                'type': op_type,
                'address': address,
                'value': value,
                'po_index': po_index
            })
            
            types_count[op_type] += 1
            cores.add(core_id)
            timestamps.append(timestamp)
            
            if op_type in ['READ', 'WRITE', 'RMW']:
                addresses.add(address)
                
                if op_type == 'READ':
                    addr_access_pattern[address]['read'] += 1
                elif op_type == 'WRITE':
                    addr_access_pattern[address]['write'] += 1
                elif op_type == 'RMW':
                    addr_access_pattern[address]['rmw'] += 1
    
    # 计算地址对
    addr_list = sorted(addresses)
    pairs = set()
    for i in range(len(addr_list)):
        for j in range(i + 1, len(addr_list)):
            pairs.add((addr_list[i], addr_list[j]))
    
    # 热点地址（访问次数最多的地址）
    hot_addrs = []
    for addr, pattern in addr_access_pattern.items():
        total_access = pattern['read'] + pattern['write'] + pattern['rmw']
        hot_addrs.append((addr, total_access, pattern))
    hot_addrs.sort(key=lambda x: x[1], reverse=True)
    
    # 时间戳分析
    time_span = 0
    if timestamps:
        time_span = max(timestamps) - min(timestamps)
    
    return {
        'file': log_file,
        'total_events': len(events),
        'addresses': len(addresses),
        'address_pairs': len(pairs),
        'types': dict(types_count),
        'cores': len(cores),
        'hot_addrs': hot_addrs[:10],  # 前 10 个热点
        'time_span': time_span,
        'addr_access_pattern': dict(addr_access_pattern)
    }

def check_consistency(result: Dict) -> List[str]:
    """检查一致性问题"""
    issues = []
    
    # 检查是否有事件
    if result['total_events'] == 0:
        issues.append("⚠️  没有事件记录")
    
    # 检查地址覆盖率
    if result['addresses'] < 10:
        issues.append(f"⚠️  地址数量偏少: {result['addresses']} (期望 > 50)")
    
    # 检查操作类型分布
    types = result['types']
    total = result['total_events']
    
    if 'READ' in types and types['READ'] / total < 0.1:
        issues.append(f"⚠️  READ 操作过少: {types['READ']/total*100:.1f}%")
    
    if 'WRITE' in types and types['WRITE'] / total < 0.1:
        issues.append(f"⚠️  WRITE 操作过少: {types['WRITE']/total*100:.1f}%")
    
    # 检查 FENCE 比例
    if 'FENCE' in types and types['FENCE'] / total > 0.5:
        issues.append(f"⚠️  FENCE 操作过多: {types['FENCE']/total*100:.1f}%")
    
    return issues

def print_detailed_report(results: List[Dict]):
    """打印详细报告"""
    if not results:
        print("没有找到日志文件")
        return
    
    print("\n" + "=" * 80)
    print("详细测试报告")
    print("=" * 80)
    print()
    
    print(f"总测试数: {len(results)}")
    print()
    
    # 1. 汇总统计
    total_events = sum(r['total_events'] for r in results)
    total_addrs = sum(r['addresses'] for r in results)
    total_pairs = sum(r['address_pairs'] for r in results)
    
    print("=" * 80)
    print("汇总统计")
    print("=" * 80)
    print(f"  总事件数: {total_events:,}")
    print(f"  总地址数: {total_addrs:,}")
    print(f"  总地址对: {total_pairs:,}")
    print(f"  平均事件/测试: {total_events / len(results):,.0f}")
    print(f"  平均地址/测试: {total_addrs / len(results):,.1f}")
    print()
    
    # 2. 操作类型分布
    print("=" * 80)
    print("全局操作类型分布")
    print("=" * 80)
    
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
    
    # 3. 每个测试的详细结果
    print("=" * 80)
    print("每个测试的详细结果")
    print("=" * 80)
    print()
    
    for i, r in enumerate(results, 1):
        filename = r['file'].split('/')[-1]
        print(f"[{i}/{len(results)}] {filename}")
        print(f"  事件数: {r['total_events']:,}")
        print(f"  地址数: {r['addresses']}")
        print(f"  地址对: {r['address_pairs']:,}")
        print(f"  核心数: {r['cores']}")
        print(f"  时间跨度: {r['time_span']:,} 周期")
        
        # 类型分布
        types_str = ", ".join(f"{k}={v:,}" for k, v in sorted(r['types'].items()))
        print(f"  类型: {types_str}")
        
        # 热点地址
        if r['hot_addrs']:
            print(f"  热点地址 (前 3):")
            for addr, count, pattern in r['hot_addrs'][:3]:
                r_count = pattern['read']
                w_count = pattern['write']
                rmw_count = pattern.get('rmw', 0)
                print(f"    {addr}: {count:,} 次访问 (R:{r_count}, W:{w_count}, RMW:{rmw_count})")
        
        # 一致性检查
        issues = check_consistency(r)
        if issues:
            print(f"  ⚠️  发现 {len(issues)} 个问题:")
            for issue in issues:
                print(f"    {issue}")
        else:
            print(f"  ✅ 无明显问题")
        
        print()
    
    # 4. 最佳/最差结果
    print("=" * 80)
    print("最佳/最差结果")
    print("=" * 80)
    print()
    
    best_events = max(results, key=lambda r: r['total_events'])
    best_addrs = max(results, key=lambda r: r['addresses'])
    best_pairs = max(results, key=lambda r: r['address_pairs'])
    
    worst_addrs = min(results, key=lambda r: r['addresses'])
    
    print(f"最多事件: {best_events['file'].split('/')[-1]}")
    print(f"  {best_events['total_events']:,} 事件")
    print()
    
    print(f"最多地址: {best_addrs['file'].split('/')[-1]}")
    print(f"  {best_addrs['addresses']} 地址, {best_addrs['address_pairs']:,} 地址对")
    print()
    
    print(f"最多地址对: {best_pairs['file'].split('/')[-1]}")
    print(f"  {best_pairs['address_pairs']:,} 地址对")
    print()
    
    print(f"最少地址: {worst_addrs['file'].split('/')[-1]}")
    print(f"  {worst_addrs['addresses']} 地址 (可能需要改进)")
    print()
    
    # 5. 覆盖率评估
    print("=" * 80)
    print("覆盖率评估")
    print("=" * 80)
    print()
    
    # 所有测试的不同地址
    all_unique_addrs = set()
    for r in results:
        for addr in r['addr_access_pattern'].keys():
            all_unique_addrs.add(addr)
    
    print(f"所有测试覆盖的不同地址: {len(all_unique_addrs)}")
    print(f"平均每个测试的地址: {total_addrs / len(results):.1f}")
    print(f"地址覆盖率: {len(all_unique_addrs) / (total_addrs / len(results)) * 100:.1f}%")
    print()
    
    print("=" * 80)
    print("✅ 详细报告完成")
    print("=" * 80)

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='增强版测试结果检查')
    parser.add_argument('--pattern',
                        default='massive_test_*_log.txt',
                        help='日志文件模式')
    
    args = parser.parse_args()
    
    print("正在分析测试结果...")
    print()
    
    log_files = sorted(glob.glob(args.pattern))
    
    if not log_files:
        print(f"未找到匹配的日志文件: {args.pattern}")
        return
    
    results = []
    for log_file in log_files:
        print(f"解析: {log_file}...")
        try:
            result = parse_log_detailed(log_file)
            results.append(result)
        except Exception as e:
            print(f"  ⚠️  错误: {e}")
    
    print()
    print_detailed_report(results)

if __name__ == '__main__':
    main()
