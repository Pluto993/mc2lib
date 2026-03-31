/*
 * Simple RISC-V test for mc2lib on gem5
 * Generates a basic memory consistency test with 2 threads
 */

#define _GNU_SOURCE
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Use RISC-V host support
#define __riscv 1
#define __riscv_xlen 64
#include "riscv_host_support.h"

// Include mc2lib RISC-V backend
#include "../../include/mc2lib/codegen/ops/riscv.hpp"

using namespace mc2lib::codegen::riscv;

#define MAX_CODE_SIZE 4096
#define MAX_THREADS 4
#define TEST_MEM_SIZE 1024

static size_t num_threads = 2;
static char *test_mem = NULL;
static pthread_barrier_t barrier;

/**
 * @brief Generate a simple Store-Load test
 * 
 * Thread 0: WRITE(x, 1); FENCE; READ(y)
 * Thread 1: WRITE(y, 1); FENCE; READ(x)
 * 
 * This is a classic litmus test (Store Buffering / SB)
 * Under SC, at least one read should see the write (not both see 0)
 */
static void
generate_sb_test(void *code, size_t thread_id)
{
    Backend backend;
    char *ptr = (char*)code;
    size_t remaining = MAX_CODE_SIZE;
    types::InstPtr at;
    
    // Get addresses for x and y
    types::Addr addr_x = (types::Addr)&test_mem[0];
    types::Addr addr_y = (types::Addr)&test_mem[64];  // Different cache line
    
    printf("Thread %zu: addr_x=0x%lx, addr_y=0x%lx\n", 
           thread_id, addr_x, addr_y);
    
    if (thread_id == 0) {
        // Thread 0: WRITE(x, 1); FENCE; READ(y, a0)
        size_t len1 = backend.Write(addr_x, 1, 0, ptr, remaining, &at);
        ptr += len1; remaining -= len1;
        
        size_t len2 = backend.FenceRW(ptr, remaining);
        ptr += len2; remaining -= len2;
        
        size_t len3 = backend.Read(addr_y, Backend::Reg::a0, len1 + len2,
                                    ptr, remaining, &at);
        ptr += len3; remaining -= len3;
    } else {
        // Thread 1: WRITE(y, 1); FENCE; READ(x, a1)
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
    
    size_t total_len = ptr - (char*)code;
    printf("Thread %zu: Generated %zu bytes of code\n", thread_id, total_len);
}

/**
 * @brief Thread function
 */
void*
thread_func(void *arg)
{
    size_t thread_id = (size_t)arg;
    
    // Allocate executable memory for test code
    void *code = mmap(NULL, MAX_CODE_SIZE,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_ANONYMOUS | MAP_PRIVATE,
                      -1, 0);
    assert(code != NULL);
    memset(code, 0, MAX_CODE_SIZE);
    
    // Generate test code
    generate_sb_test(code, thread_id);
    
    // Get callable function pointer
    void (*thread_test)() = GET_CALLABLE_THREAD(code);
    
    // Wait for all threads to be ready
    int rc = pthread_barrier_wait(&barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    
    printf("Thread %zu: Running test...\n", thread_id);
    
    // Execute test
    full_memory_barrier();
    thread_test();
    full_memory_barrier();
    
    printf("Thread %zu: Test complete\n", thread_id);
    
    // Wait for all threads to finish
    rc = pthread_barrier_wait(&barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);
    
    munmap(code, MAX_CODE_SIZE);
    return NULL;
}

int
main(int argc, char *argv[])
{
    printf("=== RISC-V Memory Consistency Test ===\n");
    printf("Threads: %zu\n", num_threads);
    
    host_init();
    
    // Allocate test memory
    test_mem = (char*)malloc(TEST_MEM_SIZE);
    assert(test_mem != NULL);
    memset(test_mem, 0, TEST_MEM_SIZE);
    
    printf("Test memory: %p (size=%d)\n", test_mem, TEST_MEM_SIZE);
    
    // Initialize barrier
    int rc = pthread_barrier_init(&barrier, NULL, num_threads);
    assert(!rc);
    
    // Create threads
    pthread_t threads[MAX_THREADS];
    
    for (size_t i = 1; i < num_threads; i++) {
        rc = pthread_create(&threads[i], NULL, thread_func, (void*)i);
        assert(!rc);
    }
    
    // Main thread also participates
    roi_begin();
    thread_func((void*)0);
    
    // Wait for other threads
    for (size_t i = 1; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    
    printf("\n=== Test Results ===\n");
    printf("x = %d\n", test_mem[0]);
    printf("y = %d\n", test_mem[64]);
    printf("(Under SC, at least one should be 1)\n");
    
    roi_end();
    
    // Cleanup
    pthread_barrier_destroy(&barrier);
    free(test_mem);
    
    printf("=== Test Complete ===\n");
    return 0;
}
