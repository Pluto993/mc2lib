#!/usr/bin/env python3
"""
Simple Test Generator - Python 版本

暂时不使用 mc2lib C++ API，直接生成测试代码模板
未来可以扩展为调用 C++ 生成器
"""

import sys
import random
from pathlib import Path


def generate_test_c_code(test_id: str, seed: int, num_iterations: int = 10) -> str:
    """生成测试 C 代码"""
    
    random.seed(seed)
    
    # 随机选择地址
    addr_x = random.choice([0, 64, 128, 192])
    addr_y = random.choice([0, 64, 128, 192])
    while addr_y == addr_x:
        addr_y = random.choice([0, 64, 128, 192])
    
    code = f"""/*
 * Generated test: {test_id}
 * Seed: {seed}
 * Auto-generated test
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Event tracer
#define MAX_EVENTS 10000

typedef enum {{
    EVENT_READ = 0,
    EVENT_WRITE = 1,
    EVENT_FENCE = 2
}} EventType;

typedef struct {{
    unsigned long timestamp;
    unsigned long seq_id;
    unsigned int core_id;
    EventType type;
    unsigned long address;
    unsigned int value;
    unsigned int po_index;
}} MemoryEvent;

static MemoryEvent events[MAX_EVENTS];
static unsigned int event_count = 0;
static unsigned int po_counter = 0;
static unsigned int core_id = 0;

static unsigned long get_timestamp() {{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}}

static void record_event(EventType type, unsigned long addr, unsigned int value) {{
    if (event_count >= MAX_EVENTS) return;
    MemoryEvent* e = &events[event_count++];
    e->timestamp = get_timestamp();
    e->seq_id = event_count - 1;
    e->core_id = core_id;
    e->type = type;
    e->address = addr;
    e->value = value;
    e->po_index = po_counter++;
}}

static volatile unsigned char test_mem[1024] __attribute__((aligned(64)));

static unsigned int inst_read(unsigned long addr) {{
    unsigned int value = test_mem[addr];
    record_event(EVENT_READ, addr, value);
    return value;
}}

static void inst_write(unsigned long addr, unsigned int value) {{
    record_event(EVENT_WRITE, addr, value);
    test_mem[addr] = (unsigned char)value;
}}

static void inst_fence() {{
    record_event(EVENT_FENCE, 0, 0);
    __sync_synchronize();
}}

static void run_test() {{
    // Store Buffering pattern with addresses: x={addr_x}, y={addr_y}
    if (core_id == 0) {{
        inst_write({addr_x}, 1);
        inst_fence();
        unsigned int r0 = inst_read({addr_y});
    }} else if (core_id == 1) {{
        inst_write({addr_y}, 1);
        inst_fence();
        unsigned int r1 = inst_read({addr_x});
    }}
}}

static void dump_trace() {{
    char filename[64];
    snprintf(filename, sizeof(filename), "memory_trace_core%u.csv", core_id);
    FILE* f = fopen(filename, "w");
    if (!f) return;
    fprintf(f, "timestamp,seq_id,core_id,type,address,value,po_index\\n");
    for (unsigned int i = 0; i < event_count && i < MAX_EVENTS; ++i) {{
        MemoryEvent* e = &events[i];
        const char* type_str;
        switch (e->type) {{
            case EVENT_READ: type_str = "READ"; break;
            case EVENT_WRITE: type_str = "WRITE"; break;
            case EVENT_FENCE: type_str = "FENCE"; break;
            default: type_str = "UNKNOWN";
        }}
        fprintf(f, "%lu,%lu,%u,%s,0x%lx,%u,%u\\n",
                e->timestamp, e->seq_id, e->core_id, type_str,
                e->address, e->value, e->po_index);
    }}
    fclose(f);
}}

int main(int argc, char* argv[]) {{
    if (argc > 1) core_id = atoi(argv[1]);
    if (core_id == 0) memset((void*)test_mem, 0, sizeof(test_mem));
    
    // Small startup delay
    for (volatile long i = 0; i < (core_id + 1) * 1000000; i++);
    
    // Run {num_iterations} iterations
    for (int iter = 0; iter < {num_iterations}; iter++) {{
        // Reset (only core 0)
        if (core_id == 0) {{
            test_mem[{addr_x}] = 0;
            test_mem[{addr_y}] = 0;
            __sync_synchronize();
        }} else {{
            for (volatile int j = 0; j < 1000; j++);
        }}
        
        run_test();
        
        // Wait between iterations
        for (volatile int j = 0; j < 100000; j++);
    }}
    
    dump_trace();
    return 0;
}}
"""
    
    return code


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <test_id> <random_seed> [output_dir]")
        sys.exit(1)
    
    test_id = sys.argv[1]
    seed = int(sys.argv[2])
    output_dir = Path(sys.argv[3]) if len(sys.argv) > 3 else Path('.')
    
    output_dir.mkdir(parents=True, exist_ok=True)
    
    # 生成代码
    code = generate_test_c_code(test_id, seed)
    
    # 写入文件
    output_file = output_dir / f'{test_id}.c'
    with open(output_file, 'w') as f:
        f.write(code)
    
    print(f"Generated test: {output_file}")
    print(f"  Test ID: {test_id}")
    print(f"  Random seed: {seed}")


if __name__ == '__main__':
    main()
