/*
 * RISC-V Memory Consistency Test - Multi-core Version (Per-core logging)
 * Each core writes its own log file
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Per-core event logger
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

// Each core has its own event buffer (no sharing)
static MemoryEvent events[MAX_EVENTS];
static unsigned int event_count = 0;
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

// Shared memory between cores
static volatile unsigned char test_mem[1024] __attribute__((aligned(64)));

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

// Store Buffering test
static void test_store_buffering() {
    unsigned long addr_x = 0;
    unsigned long addr_y = 64;
    
    if (core_id == 0) {
        // Core 0: x = 1; FENCE; r0 = y
        inst_write(addr_x, 1);
        inst_fence();
        unsigned int r0 = inst_read(addr_y);
        printf("Core 0: wrote x=1, read y=%u\n", r0);
        
    } else if (core_id == 1) {
        // Core 1: y = 1; FENCE; r1 = x
        inst_write(addr_y, 1);
        inst_fence();
        unsigned int r1 = inst_read(addr_x);
        printf("Core 1: wrote y=1, read x=%u\n", r1);
    }
}

static void dump_trace() {
    char filename[64];
    snprintf(filename, sizeof(filename), "memory_trace_core%u.csv", core_id);
    
    FILE* f = fopen(filename, "w");
    if (!f) {
        perror("Failed to open trace file");
        return;
    }
    
    fprintf(f, "timestamp,seq_id,core_id,type,address,value,po_index\n");
    
    for (unsigned int i = 0; i < event_count && i < MAX_EVENTS; i++) {
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
    printf("\n[Core %u] Trace dumped to: %s\n", core_id, filename);
    printf("[Core %u] Total events: %u\n", core_id, event_count);
}

int main(int argc, char* argv[]) {
    // Get core ID from command line
    if (argc > 1) {
        core_id = atoi(argv[1]);
    }
    
    printf("========================================\n");
    printf("RISC-V Memory Consistency Test\n");
    printf("Multi-core Version (Per-core Logging)\n");
    printf("========================================\n");
    printf("Core ID: %u (PID: %d)\n", core_id, getpid());
    printf("Test: Store Buffering (SB)\n");
    printf("========================================\n\n");
    
    // Initialize shared memory (only core 0)
    if (core_id == 0) {
        memset((void*)test_mem, 0, sizeof(test_mem));
        printf("[Core 0] Initialized shared memory at %p\n", test_mem);
        printf("[Core 0] Starting test...\n\n");
    } else {
        printf("[Core %u] Using shared memory at %p\n", core_id, test_mem);
        printf("[Core %u] Starting test...\n\n", core_id);
    }
    
    // Small delay to let all cores start
    for (volatile long i = 0; i < (core_id + 1) * 1000000; i++);
    
    // Run 10 iterations
    for (int i = 0; i < 10; i++) {
        // Simple barrier: core 0 resets, others wait
        if (core_id == 0) {
            test_mem[0] = 0;
            test_mem[64] = 0;
            __sync_synchronize();
        } else {
            // Wait for core 0 to reset
            for (volatile int j = 0; j < 1000; j++);
        }
        
        printf("[Core %u] Iteration %d\n", core_id, i);
        test_store_buffering();
        
        // Wait between iterations
        for (volatile int j = 0; j < 100000; j++);
    }
    
    printf("\n[Core %u] Test complete!\n", core_id);
    
    // Each core dumps its own trace
    dump_trace();
    
    printf("========================================\n");
    printf("[Core %u] Exiting\n", core_id);
    printf("========================================\n");
    
    return 0;
}
