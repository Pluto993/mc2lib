#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MEMORY_SIZE 1024
#define NUM_ITERATIONS 100

volatile uint64_t shared_mem[MEMORY_SIZE / sizeof(uint64_t)];

typedef struct {
    uint64_t timestamp;
    uint32_t seq_id;
    uint32_t core_id;
    uint32_t type;
    uint64_t address;
    uint64_t value;
    uint32_t po_index;
} MemoryEvent;

#define MAX_EVENTS 2000
MemoryEvent events[MAX_EVENTS];
uint32_t event_count = 0;

static inline uint64_t rdcycle() {
    uint64_t cycles;
    __asm__ volatile ("rdcycle %0" : "=r"(cycles));
    return cycles;
}

static inline void fence() {
    __asm__ volatile ("fence rw, rw" ::: "memory");
}

void record_event(uint32_t core_id, uint32_t type, uint64_t addr, uint64_t val, uint32_t po_idx) {
    if (event_count >= MAX_EVENTS) return;
    
    MemoryEvent* e = &events[event_count++];
    e->timestamp = rdcycle();
    e->seq_id = event_count - 1;
    e->core_id = core_id;
    e->type = type;
    e->address = addr;
    e->value = val;
    e->po_index = po_idx;
}

void inst_write(uint32_t core_id, uint64_t offset, uint64_t value, uint32_t po_idx) {
    shared_mem[offset / sizeof(uint64_t)] = value;
    record_event(core_id, 0, (uint64_t)&shared_mem[offset / sizeof(uint64_t)], value, po_idx);
}

uint64_t inst_read(uint32_t core_id, uint64_t offset, uint32_t po_idx) {
    uint64_t value = shared_mem[offset / sizeof(uint64_t)];
    record_event(core_id, 1, (uint64_t)&shared_mem[offset / sizeof(uint64_t)], value, po_idx);
    return value;
}

void inst_fence_record(uint32_t core_id, uint32_t po_idx) {
    fence();
    record_event(core_id, 2, 0, 0, po_idx);
}

void dump_trace() {
    printf("\n=== Memory Trace ===\n");
    
    for (uint32_t i = 0; i < event_count && i < MAX_EVENTS; i++) {
        MemoryEvent* e = &events[i];
        
        const char* type_str;
        switch (e->type) {
            case 0: type_str = "WRITE"; break;
            case 1: type_str = "READ"; break;
            case 2: type_str = "FENCE"; break;
            default: type_str = "UNKNOWN"; break;
        }
        
        printf("TRACE:%lu,%u,%u,%s,0x%lx,%lu,%u\n",
               e->timestamp, e->seq_id, e->core_id, type_str,
               e->address, e->value, e->po_index);
    }
    
    printf("=== End Trace (%u events) ===\n", event_count);
}

int main() {
    printf("========================================\n");
    printf("RISC-V Memory Consistency Test - Generation 2\n");
    printf("========================================\n");
    printf("Test ID: gen2_test_2\n");
    printf("Iterations: %d\n", NUM_ITERATIONS);
    printf("Pattern length: 95\n");
    printf("========================================\n\n");
    
    memset((void*)shared_mem, 0, MEMORY_SIZE);
    
    uint32_t core_id = 0;
    uint32_t po_idx = 0;
    
    printf("Running test...\n");
    
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        inst_read(core_id, 336, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_write(core_id, 128, 1, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_write(core_id, 128, 1, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 448, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_read(core_id, 464, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 64, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_read(core_id, 464, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_read(core_id, 128, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_write(core_id, 0, 1, po_idx++);
        inst_read(core_id, 464, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_read(core_id, 128, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_read(core_id, 464, po_idx++);
        inst_write(core_id, 192, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_read(core_id, 384, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_read(core_id, 464, po_idx++);
        inst_write(core_id, 256, 1, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 0, 1, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_read(core_id, 464, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_read(core_id, 128, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_read(core_id, 64, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_read(core_id, 464, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_read(core_id, 400, po_idx++);
        inst_read(core_id, 464, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_read(core_id, 336, po_idx++);
        inst_fence_record(core_id, po_idx++);
        inst_write(core_id, 400, 1, po_idx++);
        inst_write(core_id, 464, 1, po_idx++);
        inst_read(core_id, 464, po_idx++);
        inst_write(core_id, 336, 1, po_idx++);
        inst_read(core_id, 336, po_idx++);
    }
    
    printf("Test iterations complete\n");
    
    dump_trace();
    
    printf("\n========================================\n");
    printf("Test Statistics:\n");
    printf("========================================\n");
    printf("Total events: %u\n", event_count);
    printf("Max events: %u\n", MAX_EVENTS);
    printf("========================================\n");
    
    printf("\n✅ Test complete!\n");
    
    return 0;
}
