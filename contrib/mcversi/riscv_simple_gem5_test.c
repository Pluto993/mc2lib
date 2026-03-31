/*
 * Simplified RISC-V Memory Consistency Test
 * Single-threaded version for gem5 testing
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Simple event logger (no threading)
#define MAX_EVENTS 10000

typedef enum {
    EVENT_READ = 0,
    EVENT_WRITE = 1,
    EVENT_FENCE = 2
} EventType;

typedef struct {
    unsigned long timestamp;
    unsigned long seq_id;
    unsigned int core_id;
    EventType type;
    unsigned long address;
    unsigned int value;
    unsigned int po_index;
} MemoryEvent;

static MemoryEvent events[MAX_EVENTS];
static size_t event_count = 0;
static unsigned int po_counter = 0;
static unsigned int core_id = 0;

static unsigned long get_timestamp() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void record_event(EventType type, unsigned long addr, unsigned int value) {
    if (event_count >= MAX_EVENTS) return;
    
    MemoryEvent* e = &events[event_count++];
    e->timestamp = get_timestamp();
    e->seq_id = event_count - 1;
    e->core_id = core_id;
    e->type = type;
    e->address = addr;
    e->value = value;
    e->po_index = po_counter++;
}

// Instrumented memory
static volatile unsigned char test_mem[1024];

static unsigned int inst_read(unsigned long addr) {
    unsigned int value = test_mem[addr];
    record_event(EVENT_READ, addr, value);
    return value;
}

static void inst_write(unsigned long addr, unsigned int value) {
    record_event(EVENT_WRITE, addr, value);
    test_mem[addr] = (unsigned char)value;
}

static void inst_fence() {
    record_event(EVENT_FENCE, 0, 0);
    __sync_synchronize();
}

// Simple Store Buffering test (sequential for single core)
static void test_sb_sequential() {
    unsigned long addr_x = 0;
    unsigned long addr_y = 64;
    
    // Simulated Core 0 operations
    inst_write(addr_x, 1);
    inst_fence();
    unsigned int r0 = inst_read(addr_y);
    
    // Simulated Core 1 operations
    inst_write(addr_y, 1);
    inst_fence();
    unsigned int r1 = inst_read(addr_x);
    
    printf("Iteration result: r0=%u, r1=%u\n", r0, r1);
}

static void dump_trace(const char* filename) {
    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open trace file");
        return;
    }
    
    fprintf(f, "timestamp,seq_id,core_id,type,address,value,po_index\n");
    
    for (size_t i = 0; i < event_count; i++) {
        MemoryEvent* e = &events[i];
        const char* type_str;
        switch (e->type) {
            case EVENT_READ: type_str = "READ"; break;
            case EVENT_WRITE: type_str = "WRITE"; break;
            case EVENT_FENCE: type_str = "FENCE"; break;
            default: type_str = "UNKNOWN";
        }
        
        fprintf(f, "%lu,%lu,%u,%s,0x%lx,%u,%u\n",
                e->timestamp, e->seq_id, e->core_id, type_str,
                e->address, e->value, e->po_index);
    }
    
    fclose(f);
    printf("Trace dumped to: %s\n", filename);
    printf("Total events: %zu\n", event_count);
}

int main(int argc, char* argv[]) {
    printf("========================================\n");
    printf("RISC-V Memory Consistency Test (Simple)\n");
    printf("gem5 Compatible Version\n");
    printf("========================================\n\n");
    
    if (argc > 1) {
        core_id = atoi(argv[1]);
    }
    
    printf("Core ID: %u\n", core_id);
    printf("Running simplified sequential test...\n\n");
    
    // Initialize memory
    memset((void*)test_mem, 0, sizeof(test_mem));
    
    // Run 10 iterations
    for (int i = 0; i < 10; i++) {
        printf("Iteration %d: ", i);
        test_sb_sequential();
        
        // Reset memory
        test_mem[0] = 0;
        test_mem[64] = 0;
    }
    
    printf("\n");
    dump_trace("memory_trace.csv");
    
    printf("\n========================================\n");
    printf("Test complete!\n");
    printf("========================================\n");
    
    return 0;
}
