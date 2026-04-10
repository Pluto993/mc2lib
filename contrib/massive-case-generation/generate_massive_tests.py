#!/usr/bin/env python3
"""
超大规模内存一致性测试生成器

基于 qemu-evolution 框架，但大幅扩展规模：
- 迭代次数：100 → 10000+
- 事件数量：2000 → 100000+
- 地址空间：10 → 1000+
- 操作复杂度：简单 → 复杂模式

目标：生成能够深度探索 RISC-V 弱内存行为的超大测试
"""

import re
import random
from typing import List, Dict, Tuple, Set
from dataclasses import dataclass
from enum import Enum

# ============================================================================
# 配置参数
# ============================================================================

class ScaleMode(Enum):
    """规模模式"""
    SMALL = "small"        # 1000 次迭代，100 地址
    MEDIUM = "medium"      # 10000 次迭代，500 地址
    LARGE = "large"        # 50000 次迭代，2000 地址
    MASSIVE = "massive"    # 100000+ 次迭代，5000+ 地址
    EXTREME = "extreme"    # 500000+ 次迭代，10000+ 地址

@dataclass
class GenerationConfig:
    """生成配置"""
    mode: ScaleMode
    num_iterations: int
    num_addresses: int
    max_events: int
    pattern_length: int
    num_tests: int
    mutation_rate: float
    
    # 复杂模式参数
    enable_rmw: bool = True        # 启用 Read-Modify-Write
    enable_loops: bool = True       # 启用循环模式
    enable_dependencies: bool = True  # 启用地址依赖
    enable_multi_pattern: bool = True  # 启用多种测试模式混合

# 预设配置
CONFIGS = {
    ScaleMode.SMALL: GenerationConfig(
        mode=ScaleMode.SMALL,
        num_iterations=1000,
        num_addresses=100,
        max_events=10000,
        pattern_length=50,
        num_tests=5,
        mutation_rate=0.2
    ),
    ScaleMode.MEDIUM: GenerationConfig(
        mode=ScaleMode.MEDIUM,
        num_iterations=10000,
        num_addresses=500,
        max_events=100000,
        pattern_length=200,
        num_tests=10,
        mutation_rate=0.25
    ),
    ScaleMode.LARGE: GenerationConfig(
        mode=ScaleMode.LARGE,
        num_iterations=50000,
        num_addresses=2000,
        max_events=500000,
        pattern_length=500,
        num_tests=20,
        mutation_rate=0.3
    ),
    ScaleMode.MASSIVE: GenerationConfig(
        mode=ScaleMode.MASSIVE,
        num_iterations=100000,
        num_addresses=5000,
        max_events=1000000,
        pattern_length=1000,
        num_tests=50,
        mutation_rate=0.35
    ),
    ScaleMode.EXTREME: GenerationConfig(
        mode=ScaleMode.EXTREME,
        num_iterations=500000,
        num_addresses=10000,
        max_events=5000000,
        pattern_length=2000,
        num_tests=100,
        mutation_rate=0.4
    )
}

# ============================================================================
# 测试模式
# ============================================================================

class TestPattern(Enum):
    """测试模式"""
    STORE_BUFFERING = "sb"      # Store Buffering
    MESSAGE_PASSING = "mp"       # Message Passing
    LOAD_BUFFERING = "lb"        # Load Buffering
    RANDOM = "random"            # 随机操作
    LOOP_INTENSIVE = "loop"      # 循环密集
    RMW_HEAVY = "rmw"            # 原子操作密集
    ADDR_DEPENDENCY = "addr_dep" # 地址依赖链
    MIXED = "mixed"              # 混合模式

# ============================================================================
# 操作生成器
# ============================================================================

class OperationGenerator:
    """操作生成器"""
    
    def __init__(self, config: GenerationConfig):
        self.config = config
        self.address_pool = self._generate_address_pool()
    
    def _generate_address_pool(self) -> List[int]:
        """生成地址池"""
        # 生成对齐到 64 字节（cache line）的地址
        addresses = []
        for i in range(self.config.num_addresses):
            addr = i * 64  # 每个地址相距 64 字节
            addresses.append(addr)
        return addresses
    
    def generate_store_buffering(self, length: int) -> List[Dict]:
        """生成 Store Buffering 模式"""
        pattern = []
        
        for _ in range(length // 4):
            # 选择两个不同的地址
            addr_x, addr_y = random.sample(self.address_pool, 2)
            
            # Thread 0 模式: x=1; FENCE; r0=y
            pattern.append({'type': 'WRITE', 'address': addr_x, 'value': 1})
            pattern.append({'type': 'FENCE', 'address': 0})
            pattern.append({'type': 'READ', 'address': addr_y})
            
            # Thread 1 模式: y=1; FENCE; r1=x
            pattern.append({'type': 'WRITE', 'address': addr_y, 'value': 1})
            pattern.append({'type': 'FENCE', 'address': 0})
            pattern.append({'type': 'READ', 'address': addr_x})
        
        return pattern
    
    def generate_message_passing(self, length: int) -> List[Dict]:
        """生成 Message Passing 模式"""
        pattern = []
        
        for _ in range(length // 4):
            addr_data, addr_flag = random.sample(self.address_pool, 2)
            
            # Thread 0: data=1; FENCE; flag=1
            pattern.append({'type': 'WRITE', 'address': addr_data, 'value': 1})
            pattern.append({'type': 'FENCE', 'address': 0})
            pattern.append({'type': 'WRITE', 'address': addr_flag, 'value': 1})
            
            # Thread 1: r0=flag; FENCE; r1=data
            pattern.append({'type': 'READ', 'address': addr_flag})
            pattern.append({'type': 'FENCE', 'address': 0})
            pattern.append({'type': 'READ', 'address': addr_data})
        
        return pattern
    
    def generate_random(self, length: int) -> List[Dict]:
        """生成随机操作"""
        pattern = []
        
        for _ in range(length):
            op_type = random.choices(
                ['READ', 'WRITE', 'FENCE', 'RMW'],
                weights=[40, 40, 10, 10] if self.config.enable_rmw else [45, 45, 10, 0]
            )[0]
            
            if op_type == 'FENCE':
                pattern.append({'type': 'FENCE', 'address': 0})
            elif op_type == 'RMW':
                addr = random.choice(self.address_pool)
                pattern.append({'type': 'RMW', 'address': addr, 'value': 1})
            else:
                addr = random.choice(self.address_pool)
                if op_type == 'WRITE':
                    pattern.append({'type': 'WRITE', 'address': addr, 'value': random.randint(1, 255)})
                else:
                    pattern.append({'type': 'READ', 'address': addr})
        
        return pattern
    
    def generate_loop_intensive(self, length: int) -> List[Dict]:
        """生成循环密集模式"""
        pattern = []
        
        # 选择几个固定地址进行高频操作
        hot_addrs = random.sample(self.address_pool, min(10, len(self.address_pool)))
        
        loop_count = length // 20
        for _ in range(loop_count):
            # 内循环: 对热点地址进行多次读写
            for addr in hot_addrs:
                pattern.append({'type': 'WRITE', 'address': addr, 'value': 1})
                pattern.append({'type': 'READ', 'address': addr})
            
            # 偶尔插入 FENCE
            if random.random() < 0.3:
                pattern.append({'type': 'FENCE', 'address': 0})
        
        return pattern
    
    def generate_addr_dependency(self, length: int) -> List[Dict]:
        """生成地址依赖链"""
        pattern = []
        
        # 构建依赖链: read(a) -> write(b) -> read(c) -> ...
        chain_length = min(20, len(self.address_pool))
        num_chains = length // chain_length
        
        for _ in range(num_chains):
            chain_addrs = random.sample(self.address_pool, chain_length)
            
            for i, addr in enumerate(chain_addrs):
                if i % 2 == 0:
                    pattern.append({'type': 'READ', 'address': addr})
                else:
                    pattern.append({'type': 'WRITE', 'address': addr, 'value': i})
                
                # 偶尔插入 FENCE 破坏依赖
                if random.random() < 0.1:
                    pattern.append({'type': 'FENCE', 'address': 0})
        
        return pattern
    
    def generate_mixed(self, length: int) -> List[Dict]:
        """生成混合模式"""
        pattern = []
        
        # 混合各种模式
        chunk_size = length // 5
        
        pattern.extend(self.generate_store_buffering(chunk_size))
        pattern.extend(self.generate_message_passing(chunk_size))
        pattern.extend(self.generate_random(chunk_size))
        pattern.extend(self.generate_loop_intensive(chunk_size))
        pattern.extend(self.generate_addr_dependency(chunk_size))
        
        # 打乱顺序
        random.shuffle(pattern)
        
        return pattern[:length]
    
    def generate_pattern(self, pattern_type: TestPattern, length: int) -> List[Dict]:
        """生成指定类型的模式"""
        if pattern_type == TestPattern.STORE_BUFFERING:
            return self.generate_store_buffering(length)
        elif pattern_type == TestPattern.MESSAGE_PASSING:
            return self.generate_message_passing(length)
        elif pattern_type == TestPattern.RANDOM:
            return self.generate_random(length)
        elif pattern_type == TestPattern.LOOP_INTENSIVE:
            return self.generate_loop_intensive(length)
        elif pattern_type == TestPattern.ADDR_DEPENDENCY:
            return self.generate_addr_dependency(length)
        elif pattern_type == TestPattern.MIXED:
            return self.generate_mixed(length)
        else:
            return self.generate_random(length)

# ============================================================================
# C 代码生成器
# ============================================================================

def generate_massive_c_code(
    test_id: str,
    pattern: List[Dict],
    config: GenerationConfig
) -> str:
    """生成超大规模 C 代码"""
    
    code = f'''#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// ============================================================================
// 配置
// ============================================================================

#define MEMORY_SIZE {config.num_addresses * 64}
#define NUM_ITERATIONS {config.num_iterations}
#define MAX_EVENTS {config.max_events}

// ============================================================================
// 数据结构
// ============================================================================

volatile uint64_t shared_mem[MEMORY_SIZE / sizeof(uint64_t)];

typedef struct {{
    uint64_t timestamp;
    uint32_t seq_id;
    uint32_t core_id;
    uint32_t type;  // 0=WRITE, 1=READ, 2=FENCE, 3=RMW
    uint64_t address;
    uint64_t value;
    uint32_t po_index;
}} MemoryEvent;

MemoryEvent* events = NULL;
uint32_t event_count = 0;

// ============================================================================
// RISC-V 指令
// ============================================================================

static inline uint64_t rdcycle() {{
    uint64_t cycles;
    __asm__ __volatile__ ("rdcycle %0" : "=r"(cycles));
    return cycles;
}}

static inline void fence() {{
    __asm__ __volatile__ ("fence rw, rw" ::: "memory");
}}

static inline uint64_t atomic_swap(volatile uint64_t* addr, uint64_t val) {{
    uint64_t old;
    __asm__ __volatile__ (
        "amoswap.d %0, %2, (%1)"
        : "=r"(old)
        : "r"(addr), "r"(val)
        : "memory"
    );
    return old;
}}

// ============================================================================
// 事件记录
// ============================================================================

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

// ============================================================================
// 内存操作
// ============================================================================

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

uint64_t inst_rmw(uint32_t core_id, uint64_t offset, uint64_t value, uint32_t po_idx) {{
    uint64_t old = atomic_swap(&shared_mem[offset / sizeof(uint64_t)], value);
    record_event(core_id, 3, (uint64_t)&shared_mem[offset / sizeof(uint64_t)], old, po_idx);
    return old;
}}

// ============================================================================
// 追踪输出
// ============================================================================

void dump_trace() {{
    printf("\\n=== Memory Trace ===\\n");
    
    for (uint32_t i = 0; i < event_count && i < MAX_EVENTS; i++) {{
        MemoryEvent* e = &events[i];
        
        const char* type_str;
        switch (e->type) {{
            case 0: type_str = "WRITE"; break;
            case 1: type_str = "READ"; break;
            case 2: type_str = "FENCE"; break;
            case 3: type_str = "RMW"; break;
            default: type_str = "UNKNOWN"; break;
        }}
        
        printf("TRACE:%lu,%u,%u,%s,0x%lx,%lu,%u\\n",
               e->timestamp, e->seq_id, e->core_id, type_str,
               e->address, e->value, e->po_index);
    }}
    
    printf("=== End Trace (%u events) ===\\n", event_count);
}}

// ============================================================================
// 测试主体
// ============================================================================

int main() {{
    printf("========================================\\n");
    printf("RISC-V Massive Memory Consistency Test\\n");
    printf("========================================\\n");
    printf("Test ID: {test_id}\\n");
    printf("Scale: {config.mode.value}\\n");
    printf("Iterations: %d\\n", NUM_ITERATIONS);
    printf("Max events: %d\\n", MAX_EVENTS);
    printf("Pattern length: {len(pattern)}\\n");
    printf("Address space: {config.num_addresses} addresses\\n");
    printf("========================================\\n\\n");
    
    // 分配事件缓冲区
    events = (MemoryEvent*)malloc(MAX_EVENTS * sizeof(MemoryEvent));
    if (!events) {{
        fprintf(stderr, "Failed to allocate event buffer\\n");
        return 1;
    }}
    
    // 初始化共享内存
    memset((void*)shared_mem, 0, MEMORY_SIZE);
    
    uint32_t core_id = 0;
    uint32_t po_idx = 0;
    
    printf("Running test...\\n");
    
    time_t start_time = time(NULL);
    
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {{
        // 进度显示
        if (iter % (NUM_ITERATIONS / 10) == 0) {{
            printf("  Progress: %d%% (%d / %d)\\n", 
                   iter * 100 / NUM_ITERATIONS, iter, NUM_ITERATIONS);
        }}
        
'''
    
    # 生成操作序列
    for op in pattern:
        if op['type'] == 'WRITE':
            code += f"        inst_write(core_id, {op['address']}, {op.get('value', 1)}, po_idx++);\n"
        elif op['type'] == 'READ':
            code += f"        inst_read(core_id, {op['address']}, po_idx++);\n"
        elif op['type'] == 'FENCE':
            code += f"        inst_fence_record(core_id, po_idx++);\n"
        elif op['type'] == 'RMW':
            code += f"        inst_rmw(core_id, {op['address']}, {op.get('value', 1)}, po_idx++);\n"
    
    code += f'''    }}
    
    time_t end_time = time(NULL);
    
    printf("Test iterations complete\\n");
    printf("Elapsed time: %ld seconds\\n", end_time - start_time);
    
    dump_trace();
    
    printf("\\n========================================\\n");
    printf("Test Statistics:\\n");
    printf("========================================\\n");
    printf("Total events: %u\\n", event_count);
    printf("Max events: %u\\n", MAX_EVENTS);
    printf("Events per iteration: %.2f\\n", (float)event_count / NUM_ITERATIONS);
    printf("Elapsed time: %ld seconds\\n", end_time - start_time);
    printf("========================================\\n");
    
    printf("\\n✅ Test complete!\\n");
    
    free(events);
    
    return 0;
}}
'''
    
    return code

# ============================================================================
# 主程序
# ============================================================================

def main():
    import argparse
    
    parser = argparse.ArgumentParser(description='超大规模内存一致性测试生成器')
    parser.add_argument('--scale', 
                        choices=['small', 'medium', 'large', 'massive', 'extreme'],
                        default='medium',
                        help='测试规模')
    parser.add_argument('--pattern',
                        choices=['sb', 'mp', 'lb', 'random', 'loop', 'rmw', 'addr_dep', 'mixed'],
                        default='mixed',
                        help='测试模式')
    parser.add_argument('--output-dir',
                        default='.',
                        help='输出目录')
    
    args = parser.parse_args()
    
    # 获取配置
    scale_mode = ScaleMode(args.scale)
    config = CONFIGS[scale_mode]
    
    pattern_type = TestPattern(args.pattern)
    
    print("=" * 70)
    print("超大规模内存一致性测试生成器")
    print("=" * 70)
    print(f"规模模式: {scale_mode.value}")
    print(f"测试模式: {pattern_type.value}")
    print(f"迭代次数: {config.num_iterations:,}")
    print(f"地址数量: {config.num_addresses:,}")
    print(f"最大事件: {config.max_events:,}")
    print(f"模式长度: {config.pattern_length}")
    print(f"测试数量: {config.num_tests}")
    print("=" * 70)
    print()
    
    # 创建操作生成器
    generator = OperationGenerator(config)
    
    # 生成测试
    print(f"生成 {config.num_tests} 个测试...")
    
    for i in range(config.num_tests):
        test_id = f"massive_test_{scale_mode.value}_{pattern_type.value}_{i}"
        
        print(f"  [{i+1}/{config.num_tests}] {test_id}...")
        
        # 生成操作模式
        pattern = generator.generate_pattern(pattern_type, config.pattern_length)
        
        # 生成 C 代码
        code = generate_massive_c_code(test_id, pattern, config)
        
        # 保存
        output_file = f"{args.output_dir}/{test_id}.c"
        with open(output_file, 'w') as f:
            f.write(code)
        
        print(f"      ✓ {output_file} ({len(pattern)} operations)")
    
    print()
    print("=" * 70)
    print("生成完成！")
    print("=" * 70)
    print()
    print("编译命令:")
    print(f"  for test in {args.output_dir}/massive_test_*.c; do")
    print("    name=${test%.c}")
    print("    riscv64-linux-gnu-gcc -static -O2 -o $name $test")
    print("  done")
    print()
    print("运行命令:")
    print(f"  for test in {args.output_dir}/massive_test_{scale_mode.value}_{pattern_type.value}_*; do")
    print("    [ -x \"$test\" ] && qemu-riscv64 \"$test\" > \"${test}_log.txt\"")
    print("  done")

if __name__ == '__main__':
    main()
