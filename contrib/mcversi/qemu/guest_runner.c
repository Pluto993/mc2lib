/*
 * guest_runner.c
 *
 * Guest-side test executor for QEMU-based McVerSi workflow.
 * Loads per-thread machine code from binary files, executes them
 * concurrently with pthread barriers, then dumps both test memory
 * AND observation logs to result files.
 *
 * KEY CHANGE (Observation Log):
 *   The generated machine code uses %rcx as a pointer to per-thread
 *   observation log memory. After each Read/ReadAddrDp/RMW instruction,
 *   the code stores the observed WriteID to (%rcx) and increments %rcx.
 *
 *   This runner:
 *     1. Allocates obs_log memory for each thread (obs_log_count * sizeof(WriteID))
 *     2. Sets %rcx = obs_log_base before calling the test code
 *     3. After execution, writes obs_log data to obs_log.bin
 *
 *   The wrapper sets %rcx via inline asm before calling the test function.
 *
 * Build:
 *   gcc -O2 -lpthread -o guest_runner guest_runner.c
 *
 * Usage:
 *   ./guest_runner <shared_dir>
 *
 * Output:
 *   <shared_dir>/test_result.bin  -- raw dump of test memory
 *   <shared_dir>/obs_log.bin      -- concatenated observation logs for all threads
 */

#ifndef _GNU_SOURCE
#  define _GNU_SOURCE
#endif

#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

/* ------ Configuration ------ */

#ifndef MAX_THREADS
#  define MAX_THREADS 64
#endif

#ifndef MAX_CODE_SIZE
#  define MAX_CODE_SIZE (4096 * 16)
#endif

/* ------ Global state ------ */

static uint32_t num_threads    = 0;
static uint64_t test_mem_addr  = 0;
static uint64_t test_mem_bytes = 0;
static uint64_t test_mem_stride= 0;
static char    *test_mem       = NULL;

/* Per-thread info read from meta.bin */
static uint32_t thread_code_size[MAX_THREADS];
static uint64_t thread_code_base_ip[MAX_THREADS];
static uint32_t thread_obs_log_count[MAX_THREADS];  /* NEW: obs log entry count */

/* Per-thread loaded code */
static void    *thread_code[MAX_THREADS];

/* Per-thread observation log buffers */
static uint8_t *thread_obs_log[MAX_THREADS];
static uint32_t thread_obs_log_bytes[MAX_THREADS];

static pthread_barrier_t barrier;

/* ------ Helper functions ------ */

static inline void full_memory_barrier(void)
{
#if defined(__x86_64__)
    __asm__ __volatile__(
        "mfence\n\t"
        "mov $0, %%eax\n\t"
        "cpuid\n\t"
        ::: "memory", "cc", "rax", "rbx", "rcx", "rdx");
#elif defined(__aarch64__)
    __asm__ __volatile__("dsb sy; isb" ::: "memory");
#elif defined(__arm__)
    __asm__ __volatile__("dsb; isb" ::: "memory", "cc",
        "r0", "r1", "r2", "r3", "r4", "r5", "r6", "r7");
#else
    __sync_synchronize();
#endif
}

static void *load_code_file(const char *path, size_t expected_size)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return NULL;
    }

    fseek(f, 0, SEEK_END);
    size_t file_size = (size_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    if (file_size == 0) {
        fprintf(stderr, "Warning: %s is empty\n", path);
        fclose(f);
        return NULL;
    }

    size_t alloc_size = file_size > MAX_CODE_SIZE ? file_size : MAX_CODE_SIZE;
    void *code = mmap(NULL, alloc_size,
                      PROT_READ | PROT_WRITE | PROT_EXEC,
                      MAP_ANONYMOUS | MAP_PRIVATE,
                      -1, 0);
    if (code == MAP_FAILED) {
        perror("mmap code");
        fclose(f);
        return NULL;
    }

    memset(code, 0xcc, alloc_size);  /* fill with INT3 for safety */

    size_t nread = fread(code, 1, file_size, f);
    fclose(f);

    if (nread != file_size) {
        fprintf(stderr, "Short read on %s: expected %zu, got %zu\n",
                path, file_size, nread);
        munmap(code, alloc_size);
        return NULL;
    }

#if defined(__arm__) || defined(__aarch64__)
    __clear_cache(code, (char*)code + file_size);
#endif

    return code;
}

/*
 * Read meta.bin: now includes obs_log_count per thread.
 * Format:
 *   [uint32_t] num_threads
 *   [uint64_t] test_mem_addr
 *   [uint64_t] test_mem_bytes
 *   [uint64_t] test_mem_stride
 *   For each thread:
 *     [uint32_t] code_size
 *     [uint64_t] base_ip
 *     [uint32_t] obs_log_count    <-- NEW
 */
static int read_meta(const char *shared_dir)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/meta.bin", shared_dir);

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s: %s\n", path, strerror(errno));
        return -1;
    }

    fread(&num_threads,     sizeof(num_threads),     1, f);
    fread(&test_mem_addr,   sizeof(test_mem_addr),   1, f);
    fread(&test_mem_bytes,  sizeof(test_mem_bytes),  1, f);
    fread(&test_mem_stride, sizeof(test_mem_stride), 1, f);

    assert(num_threads > 0 && num_threads <= MAX_THREADS);

    for (uint32_t i = 0; i < num_threads; i++) {
        fread(&thread_code_size[i],      sizeof(uint32_t), 1, f);
        fread(&thread_code_base_ip[i],   sizeof(uint64_t), 1, f);
        fread(&thread_obs_log_count[i],  sizeof(uint32_t), 1, f);  /* NEW */
    }

    fclose(f);
    return 0;
}

/* ------ Thread function ------ */

typedef struct {
    uint32_t tid;
} thread_arg_t;

/*
 * call_test_code:
 *   设置 %rcx = obs_log 基址，然后调用测试代码。
 *   测试代码在每个 Read 后会执行:
 *     mov %al, (%rcx)
 *     add $1, %rcx
 *   从而把每次 Read 观测到的 WriteID 记录到 obs_log 中。
 *
 * 注意：我们使用 %rcx 作为观测日志指针，因为原始 mc2lib 生成的
 *       x86_64 代码只使用 %rax 和 %rdx 寄存器。
 *       但 full_memory_barrier() 中的 cpuid 会修改 %rcx，
 *       所以 barrier 和 fence 操作必须在 call_test_code 之外完成。
 */
static void call_test_code(void *code_ptr, uint8_t *obs_log_ptr)
{
#if defined(__x86_64__)
    __asm__ __volatile__(
        "mov %0, %%rcx\n\t"     /* %rcx = obs_log_ptr */
        "call *%1\n\t"          /* call test_fn (will use %rcx for obs log) */
        :
        : "r"((uint64_t)(uintptr_t)obs_log_ptr),
          "r"((uint64_t)(uintptr_t)code_ptr)
        : "memory", "cc", "rax", "rbx", "rcx", "rdx",
          "rsi", "rdi", "r8", "r9", "r10", "r11"
    );
#else
    /* For non-x86_64: fall back to direct call (no obs log support) */
    void (*test_fn)(void) = (void (*)(void))code_ptr;
    (void)obs_log_ptr;
    test_fn();
#endif
}

static void *thread_func(void *arg)
{
    thread_arg_t *ta = (thread_arg_t *)arg;
    uint32_t tid = ta->tid;

    /* Synchronize all threads before execution */
    int rc = pthread_barrier_wait(&barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);

    full_memory_barrier();

    /* Execute the test code with obs_log recording */
    call_test_code(thread_code[tid], thread_obs_log[tid]);

    full_memory_barrier();

    /* Synchronize after execution */
    rc = pthread_barrier_wait(&barrier);
    assert(rc == 0 || rc == PTHREAD_BARRIER_SERIAL_THREAD);

    return NULL;
}

/* ------ Main ------ */

static void usage(const char *prog)
{
    fprintf(stderr, "Usage: %s <shared_dir>\n", prog);
    fprintf(stderr, "  shared_dir: directory containing meta.bin and thread_*.bin\n");
    exit(1);
}

int main(int argc, char *argv[])
{
    if (argc < 2) usage(argv[0]);

    const char *shared_dir = argv[1];

    /* 1. Read metadata */
    if (read_meta(shared_dir) != 0) {
        return 1;
    }

    printf("=== Guest Runner [ObsLog Mode] ===\n");
    printf("Threads: %u\n", num_threads);
    printf("Test memory: %llu bytes @ 0x%llx (stride=0x%llx)\n",
           (unsigned long long)test_mem_bytes,
           (unsigned long long)test_mem_addr,
           (unsigned long long)test_mem_stride);

    /* 2. Allocate test memory at the specified address */
    if (test_mem_addr != 0) {
        test_mem = (char *)mmap((void *)(uintptr_t)test_mem_addr,
                                (size_t)test_mem_bytes,
                                PROT_READ | PROT_WRITE,
                                MAP_ANONYMOUS | MAP_PRIVATE | MAP_FIXED,
                                0, 0);
        if (test_mem == MAP_FAILED) {
            perror("mmap test_mem (MAP_FIXED)");
            fprintf(stderr, "MAP_FIXED failed at 0x%llx. "
                    "If using QEMU user mode, try: qemu-xxx -R 0x%llx\n",
                    (unsigned long long)test_mem_addr,
                    (unsigned long long)(test_mem_addr + test_mem_bytes));
            test_mem = NULL;
        }
    }

    if (test_mem == NULL) {
        test_mem = (char *)mmap(NULL, (size_t)test_mem_bytes,
                                PROT_READ | PROT_WRITE,
                                MAP_ANONYMOUS | MAP_PRIVATE,
                                -1, 0);
        if (test_mem == MAP_FAILED) {
            perror("mmap test_mem");
            return 1;
        }
        fprintf(stderr, "ERROR: test_mem at %p instead of 0x%llx -- "
                "generated code will NOT work correctly!\n",
                (void *)test_mem, (unsigned long long)test_mem_addr);
    }

    memset(test_mem, 0, (size_t)test_mem_bytes);
    full_memory_barrier();

    printf("Test memory mapped at %p\n", (void *)test_mem);

    /* 3. Allocate per-thread observation logs */
    for (uint32_t i = 0; i < num_threads; i++) {
        /* sizeof(WriteID) = 1 byte per observation entry */
        thread_obs_log_bytes[i] = thread_obs_log_count[i] * sizeof(uint8_t);
        if (thread_obs_log_bytes[i] == 0) {
            /* Even with 0 obs, allocate at least 1 page for safety */
            thread_obs_log_bytes[i] = 4096;
        }
        /* Round up to page boundary */
        size_t alloc_size = (thread_obs_log_bytes[i] + 4095) & ~(size_t)4095;
        thread_obs_log[i] = (uint8_t *)mmap(NULL, alloc_size,
                                             PROT_READ | PROT_WRITE,
                                             MAP_ANONYMOUS | MAP_PRIVATE,
                                             -1, 0);
        if (thread_obs_log[i] == MAP_FAILED) {
            perror("mmap obs_log");
            return 1;
        }
        memset(thread_obs_log[i], 0, alloc_size);
        printf("Thread %u: obs_log at %p (%u entries, %u bytes)\n",
               i, (void *)thread_obs_log[i],
               thread_obs_log_count[i], thread_obs_log_bytes[i]);
    }

    /* 4. Load per-thread code */
    for (uint32_t i = 0; i < num_threads; i++) {
        char fname[512];
        snprintf(fname, sizeof(fname), "%s/thread_%u.bin", shared_dir, i);
        thread_code[i] = load_code_file(fname, thread_code_size[i]);
        if (thread_code[i] == NULL) {
            fprintf(stderr, "Failed to load code for thread %u\n", i);
            return 1;
        }
        printf("Thread %u: loaded %u bytes of code\n", i, thread_code_size[i]);
    }

    /* 5. Initialize barrier */
    int rc = pthread_barrier_init(&barrier, NULL, num_threads);
    assert(rc == 0);

    /* 6. Spawn threads */
    pthread_t tids[MAX_THREADS];
    thread_arg_t args[MAX_THREADS];

    printf("Spawning %u threads...\n", num_threads);

    /* Set affinity (best-effort) */
    {
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(0, &cpuset);
        pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    }

    for (uint32_t i = 1; i < num_threads; i++) {
        args[i].tid = i;

        pthread_attr_t attr;
        pthread_attr_init(&attr);

        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(i % CPU_SETSIZE, &cpuset);
        pthread_attr_setaffinity_np(&attr, sizeof(cpu_set_t), &cpuset);

        rc = pthread_create(&tids[i], &attr, thread_func, &args[i]);
        assert(rc == 0);

        pthread_attr_destroy(&attr);
    }

    /* Main thread runs as thread 0 */
    args[0].tid = 0;
    thread_func(&args[0]);

    for (uint32_t i = 1; i < num_threads; i++) {
        pthread_join(tids[i], NULL);
    }

    printf("All threads finished.\n");

    /* 7. Dump test memory */
    {
        char result_path[512];
        snprintf(result_path, sizeof(result_path),
                 "%s/test_result.bin", shared_dir);

        FILE *f = fopen(result_path, "wb");
        if (!f) {
            perror("fopen test_result.bin");
            return 1;
        }
        fwrite(test_mem, 1, (size_t)test_mem_bytes, f);
        fclose(f);

        printf("Wrote %s (%llu bytes)\n", result_path,
               (unsigned long long)test_mem_bytes);
    }

    /* 8. Dump observation logs
     *
     * obs_log.bin format:
     *   For each thread (0..num_threads-1):
     *     [uint32_t] obs_log_count
     *     [uint8_t * obs_log_count] observation data
     *
     * Each entry is one WriteID (uint8_t) that a Read/ReadAddrDp/RMW observed.
     * The order matches the program order of Read ops within each thread.
     */
    {
        char obs_path[512];
        snprintf(obs_path, sizeof(obs_path), "%s/obs_log.bin", shared_dir);

        FILE *f = fopen(obs_path, "wb");
        if (!f) {
            perror("fopen obs_log.bin");
            return 1;
        }

        for (uint32_t i = 0; i < num_threads; i++) {
            uint32_t count = thread_obs_log_count[i];
            fwrite(&count, sizeof(count), 1, f);
            fwrite(thread_obs_log[i], sizeof(uint8_t), count, f);
        }

        fclose(f);
        printf("Wrote %s\n", obs_path);

        /* Print first few entries for debugging */
        for (uint32_t i = 0; i < num_threads; i++) {
            printf("  Thread %u obs_log (%u entries):", i, thread_obs_log_count[i]);
            uint32_t show = thread_obs_log_count[i] < 16 ? thread_obs_log_count[i] : 16;
            for (uint32_t j = 0; j < show; j++) {
                printf(" %02x", thread_obs_log[i][j]);
            }
            if (thread_obs_log_count[i] > 16) printf(" ...");
            printf("\n");
        }
    }

    /* 9. Cleanup */
    pthread_barrier_destroy(&barrier);

    for (uint32_t i = 0; i < num_threads; i++) {
        if (thread_code[i]) {
            munmap(thread_code[i], MAX_CODE_SIZE);
        }
        if (thread_obs_log[i] && thread_obs_log[i] != MAP_FAILED) {
            size_t alloc_size = (thread_obs_log_bytes[i] + 4095) & ~(size_t)4095;
            munmap(thread_obs_log[i], alloc_size);
        }
    }

    if (test_mem != MAP_FAILED && test_mem != NULL) {
        munmap(test_mem, (size_t)test_mem_bytes);
    }

    printf("=== Guest Runner complete ===\n");
    return 0;
}
