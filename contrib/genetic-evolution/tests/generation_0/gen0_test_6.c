/*
 * Generated test: gen0_test_6
 * Generation: 0
 * Parents: None
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Event tracer
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

static void run_test() {
    if (core_id == 0) {
        inst_write(832, 1);
        inst_fence();
        inst_read(576);
        inst_read(704);
        inst_write(896, 1);
        inst_write(448, 1);
        inst_read(128);
        inst_fence();
        inst_fence();
    } else if (core_id == 1) {
        inst_write(0, 1);
        inst_write(64, 1);
        inst_write(128, 1);
        inst_write(640, 1);
        inst_read(448);
        inst_write(960, 1);
    }
}

static void dump_trace() {
    char filename[256];
    // 输出到测试运行目录
    const char* run_dir = getenv("TEST_RUN_DIR");
    if (run_dir) {
        snprintf(filename, sizeof(filename), "%s/memory_trace_core%u.csv", run_dir, core_id);
    } else {
        snprintf(filename, sizeof(filename), "memory_trace_core%u.csv", core_id);
    }
    FILE* f = fopen(filename, "w");
    if (!f) return;
    fprintf(f, "timestamp,seq_id,core_id,type,address,value,po_index\n");
    for (unsigned int i = 0; i < event_count && i < MAX_EVENTS; ++i) {
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
}

int main(int argc, char* argv[]) {
    if (argc > 1) core_id = atoi(argv[1]);
    if (core_id == 0) memset((void*)test_mem, 0, sizeof(test_mem));
    
    // Small startup delay
    for (volatile long i = 0; i < (core_id + 1) * 1000000; i++);
    
    // Run 10 iterations
    for (int iter = 0; iter < 10; iter++) {
        // Reset (only core 0)
        if (core_id == 0) {
            memset((void*)test_mem, 0, sizeof(test_mem));
            __sync_synchronize();
        } else {
            for (volatile int j = 0; j < 1000; j++);
        }
        
        run_test();
        
        // Wait between iterations
        for (volatile int j = 0; j < 100000; j++);
    }
    
    dump_trace();
    return 0;
}
