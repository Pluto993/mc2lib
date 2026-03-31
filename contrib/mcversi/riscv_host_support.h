/*
 * RISC-V host support for mc2lib on gem5
 * Based on m5_host_support.h with RISC-V specific implementations
 */

#ifndef HOST_SUPPORT_RISCV_H_
#define HOST_SUPPORT_RISCV_H_

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/stat.h>

// Include gem5 magic instruction interface
#include "m5op.h"

#ifndef CACHELINE_SIZE
#  define CACHELINE_SIZE 64
#endif

#ifndef HOST_ZERO_TEST_MEM
#  define HOST_ZERO_TEST_MEM 1
#endif

#ifndef MAX_USED_ADDRS_SIZE
#  define MAX_USED_ADDRS_SIZE (4096*8)
#endif

#ifndef BARRIER_USE_QUIESCE
// Disable quiesce by default for RISC-V (may cause lockup with async barriers)
#  define BARRIER_USE_QUIESCE 0
#endif

#ifdef M5OP_ADDR
void *m5_mem = NULL;
#endif

#if defined(__riscv) && (__riscv_xlen == 64)

/**
 * @brief Full memory barrier for RISC-V
 * 
 * Uses FENCE instruction to ensure all memory operations complete
 * before proceeding. Clobbers registers used by test generator.
 */
static inline void
full_memory_barrier(void)
{
	__asm__ __volatile__ (
			"fence rw, rw\n\t"  // Full memory fence
			"fence.i\n\t"       // Instruction fence (for self-modifying code)
			::: "memory");
	
	// Clobber temporary registers used by test generator
	// to prevent compiler optimizations
	__asm__ __volatile__ ("" ::: 
			"t0", "t1", "t2", "t3", "t4", "t5", "t6",
			"a0", "a1", "a2", "a3", "a4", "a5", "a6", "a7");
}

/**
 * @brief Flush cache line for RISC-V
 * 
 * RISC-V base ISA doesn't have a standard cache flush instruction.
 * We use FENCE to ensure memory consistency.
 * 
 * Note: Some RISC-V implementations may have custom cache management
 * instructions (e.g., Zicbom extension).
 */
static inline void
flush_cache_line(volatile void *addr)
{
	// RISC-V base ISA doesn't have clflush equivalent
	// Use FENCE as a conservative approximation
	__asm__ __volatile__ (
			"fence rw, rw\n\t"
			::: "memory");
	
	// If Zicbom extension is available, could use:
	// cbo.flush (addr)
	// cbo.inval (addr)
}

/**
 * @brief Generate test code and prepare for execution
 * 
 * Calls gem5 magic instruction to generate test, then ensures
 * instruction cache coherency.
 */
static inline uint64_t
host_make_test_thread(void *code, uint64_t len)
{
	// Call gem5 to generate test code
	len = m5_make_test_thread(code, len);
	
	// RISC-V doesn't have __clear_cache builtin like ARM
	// Use FENCE.I to flush instruction cache
	__asm__ __volatile__ (
			"fence.i\n\t"
			::: "memory");
	
	// Additional memory barrier to prevent speculation
	full_memory_barrier();
	
	return len;
}

/**
 * @brief Get callable function pointer for RISC-V
 * 
 * RISC-V doesn't use Thumb mode like ARM, so no need to set LSB.
 * Just cast the code pointer directly.
 */
#define GET_CALLABLE_THREAD(code) ((void (*)()) code)

#else
#  error "This file is for RISC-V 64-bit only! Define __riscv and __riscv_xlen == 64"
#endif

// Common host interface functions (same for all architectures)
#define host_mark_test_mem_range m5_mark_test_mem_range

static inline void
host_verify_reset_conflict(void **used_addrs, uint64_t len)
{
	while(!m5_verify_reset_conflict(used_addrs, len));
}

static inline void
host_verify_reset_all(void **used_addrs, uint64_t len)
{
	while(!m5_verify_reset_all(used_addrs, len));
}

/**
 * @brief Precise barrier wait (tight synchronization)
 */
static inline void
barrier_wait_precise(uint64_t nt)
{
	full_memory_barrier();

#if BARRIER_USE_QUIESCE
	while(m5_barrier_async(nt, 1));
	full_memory_barrier();
#endif

	while(m5_barrier_async(nt, 0));
	full_memory_barrier();
}

/**
 * @brief Coarse barrier wait (relaxed synchronization)
 */
static inline void
barrier_wait_coarse(uint64_t nt)
{
#if BARRIER_USE_QUIESCE
	full_memory_barrier();
	while(m5_barrier_async(nt, 1));
	full_memory_barrier();
#else
	barrier_wait_precise(nt);
#endif
}

#ifdef M5OP_ADDR
/**
 * @brief Map gem5 magic instruction MMIO region
 */
static inline void
map_m5_mem(void)
{
    int fd;

    fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("Cannot open /dev/mem");
        exit(1);
    }

    m5_mem = mmap(NULL, 0x10000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, M5OP_ADDR);
    if (!m5_mem) {
        perror("Cannot mmap /dev/mem");
        exit(1);
    }
}
#endif

/**
 * @brief Initialize host support (called at program start)
 */
static inline void
host_init(void)
{
#ifdef M5OP_ADDR
	map_m5_mem();
#endif
	
	printf("RISC-V host support initialized\n");
}

/**
 * @brief Mark start of Region of Interest (ROI) for gem5 stats
 */
static inline void
roi_begin(void)
{
	m5_dumpreset_stats(0, 0);
}

/**
 * @brief Mark end of ROI and exit simulation
 */
static inline void
roi_end(void)
{
	m5_fail(0, 42);
}

#endif /* HOST_SUPPORT_RISCV_H_ */

/* vim: set ts=4 sts=4 sw=4 noet : */
