/*
 * RISC-V Memory Consistency Test with Event Tracing
 * Records all memory operations for offline consistency checking
 */

#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// Include mc2lib tracer
#include "../../include/mc2lib/tracer.hpp"

using namespace mc2lib::tracer;

#define NUM_ITERATIONS 100
#define MAX_THREADS 8

static size_t num_threads = 2;
static InstrumentedMemory* test_mem = nullptr;
static pthread_barrier_t barrier;

/**
 * @brief Store Buffering (SB) litmus test
 * 
 * Thread 0:  x = 1; FENCE; r0 = y;
 * Thread 1:  y = 1; FENCE; r1 = x;
 * 
 * Question: Can r0 == 0 && r1 == 0?
 * - Under SC: NO (forbidden)
 * - Under RVWMO: YES (allowed)
 */
static void
test_store_buffering(uint32_t core_id)
{
    volatile uint8_t* base = test_mem->getBaseAddress();
    uint64_t addr_x = 0;
    uint64_t addr_y = 64;  // Different cache line
    
    if (core_id == 0) {
        // Thread 0
        test_mem->write(addr_x, 1);
        test_mem->fence();
        uint32_t r0 = test_mem->read(addr_y);
        
    } else if (core_id == 1) {
        // Thread 1
        test_mem->write(addr_y, 1);
        test_mem->fence();
        uint32_t r1 = test_mem->read(addr_x);
    }
}

/**
 * @brief Message Passing (MP) litmus test
 * 
 * Thread 0:  x = 1; FENCE; y = 1;
 * Thread 1:  r0 = y; FENCE; r1 = x;
 * 
 * Question: Can r0 == 1 && r1 == 0?
 * - Under SC: NO
 * - Under RVWMO: Depends on fence type
 */
static void
test_message_passing(uint32_t core_id)
{
    uint64_t addr_x = 0;
    uint64_t addr_y = 64;
    
    if (core_id == 0) {
        test_mem->write(addr_x, 1);
        test_mem->fence();
        test_mem->write(addr_y, 1);
        
    } else if (core_id == 1) {
        uint32_t r0 = test_mem->read(addr_y);
        test_mem->fence();
        uint32_t r1 = test_mem->read(addr_x);
    }
}

/**
 * @brief Load Buffering (LB) litmus test
 * 
 * Thread 0:  r0 = y; FENCE; x = 1;
 * Thread 1:  r1 = x; FENCE; y = 1;
 * 
 * Question: Can r0 == 1 && r1 == 1?
 * - Under SC: NO
 * - Under RVWMO: Potentially YES
 */
static void
test_load_buffering(uint32_t core_id)
{
    uint64_t addr_x = 0;
    uint64_t addr_y = 64;
    
    if (core_id == 0) {
        uint32_t r0 = test_mem->read(addr_y);
        test_mem->fence();
        test_mem->write(addr_x, 1);
        
    } else if (core_id == 1) {
        uint32_t r1 = test_mem->read(addr_x);
        test_mem->fence();
        test_mem->write(addr_y, 1);
    }
}

/**
 * @brief Thread function
 */
void*
thread_func(void* arg)
{
    uint32_t thread_id = (uint32_t)(uintptr_t)arg;
    
    // Register with tracer
    uint32_t core_id = TRACER_REGISTER();
    printf("Thread %u registered as Core %u\n", thread_id, core_id);
    
    // Pin thread to CPU core (optional, for better timing)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(thread_id % sysconf(_SC_NPROCESSORS_ONLN), &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        // Wait for all threads to be ready
        pthread_barrier_wait(&barrier);
        
        // Reset memory
        if (core_id == 0) {
            volatile uint8_t* base = test_mem->getBaseAddress();
            memset((void*)base, 0, 128);
            __sync_synchronize();
        }
        
        pthread_barrier_wait(&barrier);
        
        // Run test (select which test to run)
        test_store_buffering(core_id);
        // test_message_passing(core_id);
        // test_load_buffering(core_id);
        
        pthread_barrier_wait(&barrier);
    }
    
    return nullptr;
}

int
main(int argc, char* argv[])
{
    printf("========================================\n");
    printf("RISC-V Memory Consistency Tracer\n");
    printf("Records events for offline checking\n");
    printf("========================================\n\n");
    
    if (argc > 1) {
        num_threads = atoi(argv[1]);
        if (num_threads < 2 || num_threads > MAX_THREADS) {
            fprintf(stderr, "Thread count must be 2-%d\n", MAX_THREADS);
            return 1;
        }
    }
    
    printf("Threads: %zu\n", num_threads);
    printf("Iterations: %d\n", NUM_ITERATIONS);
    printf("Test: Store Buffering (SB)\n\n");
    
    // Initialize tracer
    TRACER_INIT();
    
    // Allocate instrumented memory
    test_mem = new InstrumentedMemory(1024);
    printf("Test memory allocated: %p\n\n", test_mem->getBaseAddress());
    
    // Initialize barrier
    pthread_barrier_init(&barrier, nullptr, num_threads);
    
    // Create threads
    pthread_t threads[MAX_THREADS];
    
    for (size_t i = 1; i < num_threads; i++) {
        pthread_create(&threads[i], nullptr, thread_func, 
                       (void*)(uintptr_t)i);
    }
    
    // Main thread also participates
    thread_func((void*)0);
    
    // Wait for all threads
    for (size_t i = 1; i < num_threads; i++) {
        pthread_join(threads[i], nullptr);
    }
    
    printf("\n");
    TRACER_STATS();
    
    // Dump trace to file
    TRACER_DUMP("memory_trace.csv");
    
    printf("\n========================================\n");
    printf("Trace collection complete!\n");
    printf("Next step: Run consistency checker\n");
    printf("  python3 consistency_checker.py memory_trace.csv\n");
    printf("========================================\n");
    
    // Cleanup
    pthread_barrier_destroy(&barrier);
    delete test_mem;
    
    return 0;
}
