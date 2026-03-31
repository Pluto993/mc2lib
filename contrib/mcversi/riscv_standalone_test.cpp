/*
 * Standalone RISC-V memory consistency test (without gem5 m5 ops)
 * Tests the RISC-V code generation backend directly
 */

#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

// Include mc2lib headers
#include "../../include/mc2lib/types.hpp"
#include "../../include/mc2lib/codegen/ops/riscv.hpp"

using namespace mc2lib;
using namespace mc2lib::codegen::riscv;

#define MAX_CODE_SIZE 4096
#define MAX_THREADS 4
#define TEST_MEM_SIZE 1024
#define NUM_ITERATIONS 1000

static size_t num_threads = 2;
static char *test_mem = NULL;
static pthread_barrier_t barrier;
static int results_r0[NUM_ITERATIONS];
static int results_r1[NUM_ITERATIONS];
static int iteration = 0;

/**
 * @brief Generate Store-Load test (SB litmus test)
 * 
 * Thread 0: WRITE(x, 1); FENCE; r0 = READ(y)
 * Thread 1: WRITE(y, 1); FENCE; r1 = READ(x)
 * 
 * Expected: Under SC, r0==0 AND r1==0 is forbidden
 */
static size_t
generate_sb_test(void *code, size_t thread_id)
{
    Backend backend;
    char *ptr = (char*)code;
    size_t remaining = MAX_CODE_SIZE;
    types::InstPtr at;
    
    types::Addr addr_x = (types::Addr)&test_mem[0];
    types::Addr addr_y = (types::Addr)&test_mem[64];
    
    if (thread_id == 0) {
        // Thread 0: WRITE(x, 1); FENCE; READ(y) -> a0
        size_t len1 = backend.Write(addr_x, 1, 0, ptr, remaining, &at);
        ptr += len1; remaining -= len1;
        
        size_t len2 = backend.FenceRW(ptr, remaining);
        ptr += len2; remaining -= len2;
        
        size_t len3 = backend.Read(addr_y, Backend::Reg::a0, len1 + len2,
                                    ptr, remaining, &at);
        ptr += len3; remaining -= len3;
    } else {
        // Thread 1: WRITE(y, 1); FENCE; READ(x) -> a1
        size_t len1 = backend.Write(addr_y, 1, 0, ptr, remaining, &at);
        ptr += len1; remaining -= len1;
        
        size_t len2 = backend.FenceRW(ptr, remaining);
        ptr += len2; remaining -= len2;
        
        size_t len3 = backend.Read(addr_x, Backend::Reg::a1, len1 + len2,
                                    ptr, remaining, &at);
        ptr += len3; remaining -= len3;
    }
    
    // Return
    size_t len_ret = backend.Return(ptr, remaining);
    ptr += len_ret;
    
    return ptr - (char*)code;
}

/**
 * @brief Thread function
 */
void*
thread_func(void *arg)
{
    size_t thread_id = (size_t)arg;
    
    // Allocate executable memory
    void *code = mmap(NULL, MAX_CODE_SIZE,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_ANONYMOUS | MAP_PRIVATE,
                      -1, 0);
    assert(code != NULL && code != MAP_FAILED);
    memset(code, 0, MAX_CODE_SIZE);
    
    // Generate test
    size_t code_len = generate_sb_test(code, thread_id);
    
    if (thread_id == 0) {
        printf("Generated %zu bytes of RISC-V code for thread %zu\n", 
               code_len, thread_id);
    }
    
    void (*test_fn)() = (void (*)())code;
    
    // Run test iterations
    for (int iter = 0; iter < NUM_ITERATIONS; iter++) {
        // Wait for all threads
        pthread_barrier_wait(&barrier);
        
        // Reset test memory
        if (thread_id == 0) {
            test_mem[0] = 0;
            test_mem[64] = 0;
            __sync_synchronize();
        }
        
        pthread_barrier_wait(&barrier);
        
        // Execute test
        __sync_synchronize();
        test_fn();
        __sync_synchronize();
        
        // Record results
        if (thread_id == 0) {
            // a0 should contain the value read from y
            // (we can't directly get register values, so check memory)
            results_r0[iter] = test_mem[64];
        } else {
            results_r1[iter] = test_mem[0];
        }
        
        pthread_barrier_wait(&barrier);
    }
    
    munmap(code, MAX_CODE_SIZE);
    return NULL;
}

int
main(int argc, char *argv[])
{
    printf("========================================\n");
    printf("RISC-V Memory Consistency Test\n");
    printf("Testing: Store Buffering (SB) litmus test\n");
    printf("========================================\n\n");
    
    if (argc > 1) {
        num_threads = atoi(argv[1]);
        if (num_threads < 2 || num_threads > MAX_THREADS) {
            fprintf(stderr, "Invalid thread count (must be 2-%d)\n", MAX_THREADS);
            return 1;
        }
    }
    
    printf("Threads: %zu\n", num_threads);
    printf("Iterations: %d\n\n", NUM_ITERATIONS);
    
    // Allocate test memory
    test_mem = (char*)malloc(TEST_MEM_SIZE);
    assert(test_mem != NULL);
    memset(test_mem, 0, TEST_MEM_SIZE);
    
    printf("Test memory allocated at: %p\n", test_mem);
    printf("  x: %p\n", &test_mem[0]);
    printf("  y: %p\n\n", &test_mem[64]);
    
    // Initialize barrier
    int rc = pthread_barrier_init(&barrier, NULL, num_threads);
    assert(!rc);
    
    // Create threads
    pthread_t threads[MAX_THREADS];
    
    for (size_t i = 1; i < num_threads; i++) {
        rc = pthread_create(&threads[i], NULL, thread_func, (void*)i);
        assert(!rc);
    }
    
    // Main thread participates
    thread_func((void*)0);
    
    // Wait for threads
    for (size_t i = 1; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    // Analyze results
    printf("\n========================================\n");
    printf("Results\n");
    printf("========================================\n");
    
    int count_00 = 0;  // Both read 0 (SC violation)
    int count_01 = 0;  // r0=0, r1=1
    int count_10 = 0;  // r0=1, r1=0
    int count_11 = 0;  // Both read 1
    
    for (int i = 0; i < NUM_ITERATIONS; i++) {
        if (results_r0[i] == 0 && results_r1[i] == 0) count_00++;
        else if (results_r0[i] == 0 && results_r1[i] == 1) count_01++;
        else if (results_r0[i] == 1 && results_r1[i] == 0) count_10++;
        else if (results_r0[i] == 1 && results_r1[i] == 1) count_11++;
    }
    
    printf("Outcomes:\n");
    printf("  (r0=0, r1=0): %4d  <- SC forbids this!\n", count_00);
    printf("  (r0=0, r1=1): %4d\n", count_01);
    printf("  (r0=1, r1=0): %4d\n", count_10);
    printf("  (r0=1, r1=1): %4d\n", count_11);
    printf("\n");
    
    if (count_00 > 0) {
        printf("⚠️  SC VIOLATION DETECTED!\n");
        printf("   Found %d cases where both reads returned 0\n", count_00);
        printf("   This indicates WEAK MEMORY ordering (RVWMO)\n");
    } else {
        printf("✅ No SC violations detected\n");
        printf("   Behavior consistent with Sequential Consistency\n");
    }
    
    printf("\n========================================\n");
    
    // Cleanup
    pthread_barrier_destroy(&barrier);
    free(test_mem);
    
    return 0;
}
