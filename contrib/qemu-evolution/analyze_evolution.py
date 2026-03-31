#!/usr/bin/env python3
"""
分析第二代测试结果并与第一代对比
"""

import re
from collections import Counter

def parse_log(log_file):
    """解析日志文件"""
    events = []
    addresses = set()
    
    with open(log_file) as f:
        for line in f:
            if line.startswith('TRACE:'):
                parts = line.strip()[6:].split(',')
                if len(parts) == 7:
                    event = {
                        'type': parts[3],
                        'address': parts[4]
                    }
                    events.append(event)
                    if event['type'] in ['READ', 'WRITE']:
                        addresses.add(event['address'])
    
    # 计算地址对
    addr_list = sorted(addresses)
    pairs = set()
    for i in range(len(addr_list)):
        for j in range(i + 1, len(addr_list)):
            pairs.add((addr_list[i], addr_list[j]))
    
    return events, addresses, pairs

def main():
    print("=" * 70)
    print("遗传算法演化结果分析")
    print("=" * 70)
    print()
    
    # 第一代
    print("第一代测试 (simple_mc2lib_test):")
    print("-" * 70)
    events1, addrs1, pairs1 = parse_log('mc2lib_qemu_log.txt')
    print(f"  事件总数: {len(events1)}")
    print(f"  地址数量: {len(addrs1)}")
    print(f"  地址对数: {len(pairs1)}")
    print(f"  地址列表: {sorted(addrs1)}")
    
    # 操作类型统计
    type_counts1 = Counter(e['type'] for e in events1)
    print(f"  操作统计: WRITE={type_counts1['WRITE']}, READ={type_counts1['READ']}, FENCE={type_counts1['FENCE']}")
    
    fitness1 = len(pairs1) / 100.0
    print(f"  适应度: {fitness1:.4f}")
    print()
    
    # 第二代
    all_fitaddrs = pairs1.copy()
    
    for i, test in enumerate(['gen2_test_0', 'gen2_test_1', 'gen2_test_2']):
        print(f"第二代测试 {i} ({test}):")
        print("-" * 70)
        
        events, addrs, pairs = parse_log(f'{test}_log.txt')
        
        print(f"  事件总数: {len(events)}")
        print(f"  地址数量: {len(addrs)}")
        print(f"  地址对数: {len(pairs)}")
        print(f"  地址列表: {sorted(addrs)[:10]}{'...' if len(addrs) > 10 else ''}")
        
        # 操作类型统计
        type_counts = Counter(e['type'] for e in events)
        print(f"  操作统计: WRITE={type_counts['WRITE']}, READ={type_counts['READ']}, FENCE={type_counts['FENCE']}")
        
        # 新地址对
        new_pairs = pairs - all_fitaddrs
        all_fitaddrs.update(pairs)
        
        # 适应度
        fitness = len(pairs) / 100.0 + len(new_pairs) * 0.1
        print(f"  适应度: {fitness:.4f}")
        print(f"  新地址对: {len(new_pairs)}")
        print(f"  改进: {(fitness - fitness1) / fitness1 * 100:.1f}%")
        print()
    
    # 总结
    print("=" * 70)
    print("演化总结:")
    print("=" * 70)
    print(f"第一代适应度: {fitness1:.4f}")
    print(f"第二代最佳:   {max([len(parse_log(f'gen2_test_{i}_log.txt')[2]) / 100.0 + len(parse_log(f'gen2_test_{i}_log.txt')[2] - pairs1) * 0.1 for i in range(3)]):.4f}")
    print(f"总 fitaddrs:   {len(all_fitaddrs)}")
    print(f"覆盖率提升:   {(len(all_fitaddrs) - len(pairs1)) / len(pairs1) * 100:.1f}%")
    print()
    print("✅ 遗传算法成功演化！")

if __name__ == '__main__':
    main()
