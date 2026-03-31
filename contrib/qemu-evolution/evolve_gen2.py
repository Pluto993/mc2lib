#!/usr/bin/env python3
"""
基于 QEMU 日志的遗传算法迭代
分析第一代测试结果，生成改进的第二代测试
"""

import re
import random
from typing import List, Dict, Tuple

def parse_trace_log(log_file: str) -> Tuple[List[Dict], set]:
    """
    解析 TRACE 日志
    
    Returns:
        (events, address_pairs)
    """
    events = []
    addresses = set()
    
    with open(log_file) as f:
        for line in f:
            if not line.startswith('TRACE:'):
                continue
            
            # TRACE:<timestamp>,<seq_id>,<core_id>,<type>,<address>,<value>,<po_index>
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
            
            if event['type'] in ['READ', 'WRITE']:
                addresses.add(event['address'])
    
    # 生成地址对
    addr_list = sorted(addresses)
    pairs = set()
    for i in range(len(addr_list)):
        for j in range(i + 1, len(addr_list)):
            pairs.add((addr_list[i], addr_list[j]))
    
    return events, pairs

def calculate_fitness(events: List[Dict], address_pairs: set, all_fitaddrs: set) -> float:
    """计算适应度"""
    # 基础覆盖率
    base_coverage = len(address_pairs) / 100.0
    
    # 新地址对奖励
    new_pairs = address_pairs - all_fitaddrs
    new_pair_bonus = len(new_pairs) * 0.1
    
    # 总适应度
    fitness = base_coverage + new_pair_bonus
    
    return fitness

def extract_pattern(events: List[Dict]) -> List[Dict]:
    """提取操作模式"""
    pattern = []
    
    for event in events:
        if event['type'] == 'FENCE':
            pattern.append({'type': 'FENCE', 'address': 0})
        elif event['type'] == 'WRITE':
            # 提取地址（转为偏移）
            addr_int = int(event['address'], 16)
            offset = (addr_int % 1024) & ~0x7  # 对齐到 8 字节
            pattern.append({'type': 'WRITE', 'address': offset, 'value': 1})
        elif event['type'] == 'READ':
            addr_int = int(event['address'], 16)
            offset = (addr_int % 1024) & ~0x7
            pattern.append({'type': 'READ', 'address': offset})
    
    return pattern

def mutate_pattern(pattern: List[Dict], mutation_rate: float = 0.2) -> List[Dict]:
    """变异操作模式"""
    new_pattern = []
    
    for op in pattern:
        if random.random() < mutation_rate:
            # 变异类型
            mutation_type = random.choice(['change_addr', 'change_type', 'insert', 'delete'])
            
            if mutation_type == 'change_addr' and op['type'] != 'FENCE':
                # 改变地址
                new_addr = random.choice([0, 64, 128, 192, 256, 320, 384, 448, 512])
                new_pattern.append({**op, 'address': new_addr})
            
            elif mutation_type == 'change_type':
                # 改变操作类型
                if op['type'] == 'READ':
                    new_pattern.append({'type': 'WRITE', 'address': op['address'], 'value': 1})
                elif op['type'] == 'WRITE':
                    new_pattern.append({'type': 'READ', 'address': op['address']})
                else:
                    new_pattern.append(op)
            
            elif mutation_type == 'insert':
                # 插入新操作
                new_pattern.append(op)
                new_op_type = random.choice(['READ', 'WRITE', 'FENCE'])
                if new_op_type == 'FENCE':
                    new_pattern.append({'type': 'FENCE', 'address': 0})
                elif new_op_type == 'WRITE':
                    new_pattern.append({
                        'type': 'WRITE',
                        'address': random.choice([0, 64, 128, 192, 256]),
                        'value': 1
                    })
                else:
                    new_pattern.append({
                        'type': 'READ',
                        'address': random.choice([0, 64, 128, 192, 256])
                    })
            
            elif mutation_type == 'delete':
                # 删除操作
                pass
            
            else:
                new_pattern.append(op)
        else:
            new_pattern.append(op)
    
    return new_pattern

def generate_c_code(test_id: str, pattern: List[Dict], iterations: int = 100) -> str:
    """生成 C 代码"""
    code = f'''#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_SIZE 1024
#define NUM_ITERATIONS {iterations}

volatile uint64_t shared_mem[MEMORY_SIZE / sizeof(uint64_t)];

typedef struct {{
    uint64_t timestamp;
    uint32_t seq_id;
    uint32_t core_id;
    uint32_t type;
    uint64_t address;
    uint64_t value;
    uint32_t po_index;
}} MemoryEvent;

#define MAX_EVENTS 2000
MemoryEvent events[MAX_EVENTS];
uint32_t event_count = 0;

static inline uint64_t rdcycle() {{
    uint64_t cycles;
    __asm__ volatile ("rdcycle %0" : "=r"(cycles));
    return cycles;
}}

static inline void fence() {{
    __asm__ volatile ("fence rw, rw" ::: "memory");
}}

void record_event(uint32_t core_id, uint32_t type, uint64_t addr, uint64_t val, uint32_t po_idx) {{
    if (event_count >= MAX_EVENTS) return;
    
    MemoryEvent* e = &events[event_count++];
    e->timestamp = rdcycle();
    e->seq_id = event_count - 1;
    e->core_id = core_id;
    e->type = type;
    e->address = addr;
    e->value = val;
    e->po_index = po_idx;
}}

void inst_write(uint32_t core_id, uint64_t offset, uint64_t value, uint32_t po_idx) {{
    shared_mem[offset / sizeof(uint64_t)] = value;
    record_event(core_id, 0, (uint64_t)&shared_mem[offset / sizeof(uint64_t)], value, po_idx);
}}

uint64_t inst_read(uint32_t core_id, uint64_t offset, uint32_t po_idx) {{
    uint64_t value = shared_mem[offset / sizeof(uint64_t)];
    record_event(core_id, 1, (uint64_t)&shared_mem[offset / sizeof(uint64_t)], value, po_idx);
    return value;
}}

void inst_fence_record(uint32_t core_id, uint32_t po_idx) {{
    fence();
    record_event(core_id, 2, 0, 0, po_idx);
}}

void dump_trace() {{
    printf("\\n=== Memory Trace ===\\n");
    
    for (uint32_t i = 0; i < event_count && i < MAX_EVENTS; i++) {{
        MemoryEvent* e = &events[i];
        
        const char* type_str;
        switch (e->type) {{
            case 0: type_str = "WRITE"; break;
            case 1: type_str = "READ"; break;
            case 2: type_str = "FENCE"; break;
            default: type_str = "UNKNOWN"; break;
        }}
        
        printf("TRACE:%lu,%u,%u,%s,0x%lx,%lu,%u\\n",
               e->timestamp, e->seq_id, e->core_id, type_str,
               e->address, e->value, e->po_index);
    }}
    
    printf("=== End Trace (%u events) ===\\n", event_count);
}}

int main() {{
    printf("========================================\\n");
    printf("RISC-V Memory Consistency Test - Generation 2\\n");
    printf("========================================\\n");
    printf("Test ID: {test_id}\\n");
    printf("Iterations: %d\\n", NUM_ITERATIONS);
    printf("Pattern length: {len(pattern)}\\n");
    printf("========================================\\n\\n");
    
    memset((void*)shared_mem, 0, MEMORY_SIZE);
    
    uint32_t core_id = 0;
    uint32_t po_idx = 0;
    
    printf("Running test...\\n");
    
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {{
'''
    
    # 生成操作序列
    for op in pattern:
        if op['type'] == 'WRITE':
            code += f"        inst_write(core_id, {op['address']}, {op.get('value', 1)}, po_idx++);\n"
        elif op['type'] == 'READ':
            code += f"        inst_read(core_id, {op['address']}, po_idx++);\n"
        elif op['type'] == 'FENCE':
            code += f"        inst_fence_record(core_id, po_idx++);\n"
    
    code += f'''    }}
    
    printf("Test iterations complete\\n");
    
    dump_trace();
    
    printf("\\n========================================\\n");
    printf("Test Statistics:\\n");
    printf("========================================\\n");
    printf("Total events: %u\\n", event_count);
    printf("Max events: %u\\n", MAX_EVENTS);
    printf("========================================\\n");
    
    printf("\\n✅ Test complete!\\n");
    
    return 0;
}}
'''
    
    return code

def main():
    print("=" * 60)
    print("遗传算法 - 第二代测试生成")
    print("=" * 60)
    print()
    
    # 解析第一代日志
    print("分析第一代测试结果...")
    log_file = 'mc2lib_qemu_log.txt'
    events, address_pairs = parse_trace_log(log_file)
    
    print(f"  事件数: {len(events)}")
    print(f"  地址对: {len(address_pairs)}")
    print(f"  地址: {sorted(set(e['address'] for e in events if e['type'] in ['READ', 'WRITE']))}")
    print()
    
    # 计算第一代适应度
    all_fitaddrs = set()
    fitness_gen1 = calculate_fitness(events, address_pairs, all_fitaddrs)
    all_fitaddrs.update(address_pairs)
    
    print(f"第一代适应度: {fitness_gen1:.4f}")
    print(f"Fitaddrs: {len(all_fitaddrs)}")
    print()
    
    # 提取模式
    print("提取操作模式...")
    pattern = extract_pattern(events)
    print(f"  模式长度: {len(pattern)}")
    print(f"  前 10 个操作: {pattern[:10]}")
    print()
    
    # 变异生成第二代
    print("变异生成第二代测试...")
    num_tests = 3
    
    for i in range(num_tests):
        test_id = f"gen2_test_{i}"
        
        # 变异模式
        new_pattern = mutate_pattern(pattern, mutation_rate=0.3)
        
        # 生成 C 代码
        code = generate_c_code(test_id, new_pattern, iterations=100)
        
        # 保存
        with open(f'{test_id}.c', 'w') as f:
            f.write(code)
        
        print(f"  ✓ {test_id}.c ({len(new_pattern)} operations)")
    
    print()
    print("=" * 60)
    print("第二代测试生成完成！")
    print("=" * 60)
    print()
    print("运行测试:")
    print("  for test in gen2_test_*.c; do")
    print("    name=\${test%.c}")
    print("    riscv64-linux-gnu-gcc -static -O2 -o \$name \$test")
    print("    qemu-riscv64 ./\$name > \${name}_log.txt")
    print("  done")

if __name__ == '__main__':
    main()
