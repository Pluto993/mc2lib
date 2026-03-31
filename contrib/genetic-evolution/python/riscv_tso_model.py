#!/usr/bin/env python3
"""
RISC-V TSO Memory Model Support for mc2lib

实现 RISC-V Total Store Order (TSO) 内存模型的测试生成
基于 RISC-V Ztso 扩展规范
"""

import random
from enum import Enum
from typing import List, Dict, Tuple
from dataclasses import dataclass


class MemoryModel(Enum):
    """支持的内存模型"""
    RVWMO = "rvwmo"  # RISC-V Weak Memory Ordering (默认)
    TSO = "tso"       # Total Store Order (Ztso 扩展)


class InstructionType(Enum):
    """指令类型"""
    LOAD = "ld"
    STORE = "sd"
    FENCE = "fence"
    FENCE_TSO = "fence.tso"  # TSO 专用 fence


@dataclass
class Instruction:
    """指令"""
    type: InstructionType
    address: int = 0
    value: int = 0
    ordering: str = "rw,rw"  # fence 排序规则
    
    def to_riscv_asm(self) -> str:
        """转换为 RISC-V 汇编"""
        if self.type == InstructionType.LOAD:
            return f"ld t0, {self.address}(x0)"
        elif self.type == InstructionType.STORE:
            return f"sd {self.value}, {self.address}(x0)"
        elif self.type == InstructionType.FENCE:
            return f"fence {self.ordering}"
        elif self.type == InstructionType.FENCE_TSO:
            return "fence.tso"
        return ""


class RISCVTSOModel:
    """RISC-V TSO 内存模型"""
    
    def __init__(self, model: MemoryModel = MemoryModel.RVWMO):
        self.model = model
        self.fence_type = InstructionType.FENCE_TSO if model == MemoryModel.TSO else InstructionType.FENCE
    
    def get_fence_instruction(self, ordering: str = "rw,rw") -> Instruction:
        """获取适当的 fence 指令"""
        if self.model == MemoryModel.TSO:
            # TSO 模型使用 fence.tso
            return Instruction(InstructionType.FENCE_TSO)
        else:
            # RVWMO 使用标准 fence
            return Instruction(InstructionType.FENCE, ordering=ordering)
    
    def is_tso_compliant(self, program: List[Instruction]) -> bool:
        """检查程序是否符合 TSO 要求"""
        if self.model != MemoryModel.TSO:
            return True
        
        # TSO 要求：
        # 1. 所有 store-load 之间必须有 fence.tso
        # 2. 不能有 load-load, store-store reordering
        
        for i in range(len(program) - 1):
            curr = program[i]
            next_instr = program[i + 1]
            
            # 检查 store-load 排序
            if (curr.type == InstructionType.STORE and 
                next_instr.type == InstructionType.LOAD):
                # 必须有 fence.tso 在中间
                if i + 2 >= len(program) or program[i + 1].type != InstructionType.FENCE_TSO:
                    return False
        
        return True


class TSOTestGenerator:
    """TSO 测试生成器"""
    
    def __init__(self, model: MemoryModel = MemoryModel.RVWMO):
        self.model = RISCVTSOModel(model)
        self.addresses = [0, 64, 128, 192, 256, 320, 384, 448]
    
    def generate_store_buffering_test(self, seed: int) -> Tuple[List[Instruction], List[Instruction]]:
        """
        生成 Store Buffering 测试（TSO 关键测试）
        
        Thread 0: x = 1; fence; r0 = y
        Thread 1: y = 1; fence; r1 = x
        
        RVWMO: 允许 r0 = 0 && r1 = 0
        TSO:   禁止 r0 = 0 && r1 = 0
        """
        random.seed(seed)
        
        addr_x = random.choice(self.addresses)
        addr_y = random.choice([a for a in self.addresses if a != addr_x])
        
        thread0 = [
            Instruction(InstructionType.STORE, addr_x, 1),
            self.model.get_fence_instruction(),
            Instruction(InstructionType.LOAD, addr_y)
        ]
        
        thread1 = [
            Instruction(InstructionType.STORE, addr_y, 1),
            self.model.get_fence_instruction(),
            Instruction(InstructionType.LOAD, addr_x)
        ]
        
        return thread0, thread1
    
    def generate_message_passing_test(self, seed: int) -> Tuple[List[Instruction], List[Instruction]]:
        """
        生成 Message Passing 测试
        
        Thread 0: x = 1; fence; y = 1
        Thread 1: r0 = y; fence; r1 = x
        
        期望: r0 = 1 => r1 = 1 (因果关系)
        """
        random.seed(seed + 1)
        
        addr_x = random.choice(self.addresses)
        addr_y = random.choice([a for a in self.addresses if a != addr_x])
        
        thread0 = [
            Instruction(InstructionType.STORE, addr_x, 1),
            self.model.get_fence_instruction("w,w"),  # store-store fence
            Instruction(InstructionType.STORE, addr_y, 1)
        ]
        
        thread1 = [
            Instruction(InstructionType.LOAD, addr_y),
            self.model.get_fence_instruction("r,r"),  # load-load fence
            Instruction(InstructionType.LOAD, addr_x)
        ]
        
        return thread0, thread1
    
    def generate_load_buffering_test(self, seed: int) -> Tuple[List[Instruction], List[Instruction]]:
        """
        生成 Load Buffering 测试
        
        Thread 0: r0 = y; fence; x = 1
        Thread 1: r1 = x; fence; y = 1
        
        不应该出现: r0 = 1 && r1 = 1 (循环依赖)
        """
        random.seed(seed + 2)
        
        addr_x = random.choice(self.addresses)
        addr_y = random.choice([a for a in self.addresses if a != addr_x])
        
        thread0 = [
            Instruction(InstructionType.LOAD, addr_y),
            self.model.get_fence_instruction(),
            Instruction(InstructionType.STORE, addr_x, 1)
        ]
        
        thread1 = [
            Instruction(InstructionType.LOAD, addr_x),
            self.model.get_fence_instruction(),
            Instruction(InstructionType.STORE, addr_y, 1)
        ]
        
        return thread0, thread1
    
    def generate_c_code(self, test_name: str, thread0: List[Instruction], 
                       thread1: List[Instruction], iterations: int = 10) -> str:
        """生成 C 代码测试"""
        
        # 生成线程 0 代码
        thread0_code = []
        for idx, instr in enumerate(thread0):
            if instr.type == InstructionType.STORE:
                thread0_code.append(f"    inst_write(0, {instr.address}, {instr.value}, po_idx++);")
            elif instr.type == InstructionType.LOAD:
                thread0_code.append(f"    uint64_t r0 = inst_read(0, {instr.address}, po_idx++);")
            elif instr.type in [InstructionType.FENCE, InstructionType.FENCE_TSO]:
                thread0_code.append(f"    inst_fence_record(0, po_idx++);")
        
        # 生成线程 1 代码
        thread1_code = []
        for idx, instr in enumerate(thread1):
            if instr.type == InstructionType.STORE:
                thread1_code.append(f"    inst_write(1, {instr.address}, {instr.value}, po_idx++);")
            elif instr.type == InstructionType.LOAD:
                thread1_code.append(f"    uint64_t r1 = inst_read(1, {instr.address}, po_idx++);")
            elif instr.type in [InstructionType.FENCE, InstructionType.FENCE_TSO]:
                thread1_code.append(f"    inst_fence_record(1, po_idx++);")
        
        model_name = "TSO" if self.model.model == MemoryModel.TSO else "RVWMO"
        
        code = f'''/*
 * RISC-V {model_name} Memory Model Test
 * Test: {test_name}
 * Generated by mc2lib TSO extension
 */

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>

#define MEMORY_SIZE 1024
#define NUM_ITERATIONS {iterations}
#define NUM_CORES 2

volatile uint64_t shared_mem[MEMORY_SIZE / sizeof(uint64_t)];

typedef struct {{
    uint64_t timestamp;
    uint32_t seq_id;
    uint32_t core_id;
    uint32_t type;  // 0=WRITE, 1=READ, 2=FENCE
    uint64_t address;
    uint64_t value;
    uint32_t po_index;
}} MemoryEvent;

#define MAX_EVENTS 1000
MemoryEvent events[NUM_CORES][MAX_EVENTS];
uint32_t event_count[NUM_CORES] = {{0}};

pthread_barrier_t barrier;

static inline uint64_t rdcycle() {{
    uint64_t cycles;
    __asm__ volatile ("rdcycle %0" : "=r"(cycles));
    return cycles;
}}

static inline void fence() {{
#ifdef RISCV_TSO
    __asm__ volatile ("fence.tso" ::: "memory");
#else
    __asm__ volatile ("fence rw, rw" ::: "memory");
#endif
}}

void record_event(uint32_t core_id, uint32_t type, uint64_t addr, uint64_t val, uint32_t po_idx) {{
    if (event_count[core_id] >= MAX_EVENTS) return;
    
    MemoryEvent* e = &events[core_id][event_count[core_id]++];
    e->timestamp = rdcycle();
    e->seq_id = event_count[core_id] - 1;
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

void* thread_0(void* arg) {{
    uint32_t po_idx = 0;
    
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {{
        pthread_barrier_wait(&barrier);
        
{chr(10).join(thread0_code)}
        
        pthread_barrier_wait(&barrier);
        
        if (iter == 0) {{
            memset((void*)shared_mem, 0, MEMORY_SIZE);
            fence();
        }}
    }}
    
    return NULL;
}}

void* thread_1(void* arg) {{
    uint32_t po_idx = 0;
    
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {{
        pthread_barrier_wait(&barrier);
        
{chr(10).join(thread1_code)}
        
        pthread_barrier_wait(&barrier);
    }}
    
    return NULL;
}}

void dump_trace() {{
    printf("\\n=== Memory Trace ({model_name} Model) ===\\n");
    printf("timestamp,seq_id,core_id,type,address,value,po_index\\n");
    
    for (uint32_t core = 0; core < NUM_CORES; core++) {{
        for (uint32_t i = 0; i < event_count[core] && i < MAX_EVENTS; i++) {{
            MemoryEvent* e = &events[core][i];
            
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
    }}
    
    printf("=== End Trace ===\\n");
}}

int main() {{
    printf("========================================\\n");
    printf("RISC-V {model_name} Memory Model Test\\n");
    printf("========================================\\n");
    printf("Test: {test_name}\\n");
    printf("Iterations: %d\\n", NUM_ITERATIONS);
    printf("========================================\\n\\n");
    
    memset((void*)shared_mem, 0, MEMORY_SIZE);
    pthread_barrier_init(&barrier, NULL, NUM_CORES);
    
    pthread_t threads[NUM_CORES];
    pthread_create(&threads[0], NULL, thread_0, NULL);
    pthread_create(&threads[1], NULL, thread_1, NULL);
    
    pthread_join(threads[0], NULL);
    pthread_join(threads[1], NULL);
    
    dump_trace();
    
    printf("\\n========================================\\n");
    printf("Test Statistics:\\n");
    printf("========================================\\n");
    for (uint32_t core = 0; core < NUM_CORES; core++) {{
        printf("Core %u: %u events\\n", core, event_count[core]);
    }}
    printf("========================================\\n");
    
    printf("\\n✅ Test complete!\\n");
    
    pthread_barrier_destroy(&barrier);
    
    return 0;
}}
'''
        
        return code


def main():
    """示例：生成 TSO 测试"""
    
    print("=" * 60)
    print("RISC-V TSO Memory Model Test Generator")
    print("=" * 60)
    print()
    
    # 创建 TSO 和 RVWMO 生成器
    tso_gen = TSOTestGenerator(MemoryModel.TSO)
    rvwmo_gen = TSOTestGenerator(MemoryModel.RVWMO)
    
    # 生成 Store Buffering 测试
    print("Generating Store Buffering tests...")
    
    # TSO 版本
    t0, t1 = tso_gen.generate_store_buffering_test(seed=42)
    code_tso = tso_gen.generate_c_code("Store_Buffering", t0, t1, iterations=100)
    with open("store_buffering_tso.c", "w") as f:
        f.write(code_tso)
    print("  ✓ store_buffering_tso.c")
    
    # RVWMO 版本
    t0, t1 = rvwmo_gen.generate_store_buffering_test(seed=42)
    code_rvwmo = rvwmo_gen.generate_c_code("Store_Buffering", t0, t1, iterations=100)
    with open("store_buffering_rvwmo.c", "w") as f:
        f.write(code_rvwmo)
    print("  ✓ store_buffering_rvwmo.c")
    
    # 生成 Message Passing 测试
    print("\\nGenerating Message Passing tests...")
    
    t0, t1 = tso_gen.generate_message_passing_test(seed=42)
    code = tso_gen.generate_c_code("Message_Passing", t0, t1, iterations=100)
    with open("message_passing_tso.c", "w") as f:
        f.write(code)
    print("  ✓ message_passing_tso.c")
    
    # 生成 Load Buffering 测试
    print("\\nGenerating Load Buffering tests...")
    
    t0, t1 = tso_gen.generate_load_buffering_test(seed=42)
    code = tso_gen.generate_c_code("Load_Buffering", t0, t1, iterations=100)
    with open("load_buffering_tso.c", "w") as f:
        f.write(code)
    print("  ✓ load_buffering_tso.c")
    
    print()
    print("=" * 60)
    print("✅ All TSO tests generated!")
    print("=" * 60)
    print()
    print("Compile with:")
    print("  gcc -DRISCV_TSO -pthread -o test_tso store_buffering_tso.c")
    print("  gcc -pthread -o test_rvwmo store_buffering_rvwmo.c")


if __name__ == '__main__':
    main()
