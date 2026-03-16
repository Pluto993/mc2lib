/*
 * host_generate.cpp
 *
 * Initial test code generator for QEMU-based McVerSi workflow.
 * Creates the first round of test code and initializes the GA population.
 *
 * KEY CHANGE (Observation Log):
 *   Uses Backend_X86_64_ObsLog instead of plain Backend_X86_64.
 *   Every Read/ReadAddrDp/RMW in the generated machine code now appends
 *   an extra store instruction that saves the observed WriteID to a
 *   per-thread observation log area.
 *
 *   The guest_runner must:
 *     1. Allocate an obs_log region for each thread
 *     2. Set %rcx to the thread's obs_log base before calling the test code
 *     3. Dump the obs_log alongside test_mem after execution
 *
 *   The host_verify reads the obs_log to reconstruct exact rf/co relations.
 *
 * Build on host (x86_64):
 *   g++ -std=c++14 -O2 -I../../../include host_generate.cpp -o host_generate
 *
 * Usage:
 *   ./host_generate <num_threads> <ops_per_thread> <test_mem_bytes>
 *                   <test_mem_stride> <test_mem_addr> <output_dir>
 *                   [--population <N>] [--mutation_rate <F>]
 *
 * Output:
 *   <output_dir>/thread_0.bin ... thread_N.bin  -- per-thread machine code
 *   <output_dir>/meta.bin                       -- test metadata (includes obs_log info)
 *   <output_dir>/pool_state.bin                 -- serialized GA population
 */

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "mc2lib/codegen/cats.hpp"
#include "mc2lib/codegen/compiler.hpp"
#include "mc2lib/codegen/ops/x86_64_qemu.hpp"
#include "mc2lib/codegen/rit.hpp"
#include "mc2lib/mcversi.hpp"
#include "mc2lib/memconsistency/cats.hpp"
#include "mc2lib/simplega.hpp"

using namespace mc2lib;
using namespace mc2lib::codegen;
using namespace mc2lib::codegen::strong;
using namespace mc2lib::memconsistency;

// ---- Type aliases ----
// Operation and MemOperation must use the abstract strong::Backend as their
// template parameter, because all concrete Op subclasses (Read, Write, etc.)
// in strong.hpp inherit from Op<strong::Backend, EvtStateCats>.
// The Compiler's second template parameter must be a *concrete* Backend class
// so it can be stored as a value member.  Backend_X86_64_ObsLog inherits from
// Backend_X86_64 which inherits from strong::Backend, so the pointer conversion
// in Compiler::Emit (passing &backend_ as strong::Backend*) works correctly.
typedef Op<Backend, EvtStateCats> Operation;
typedef MemOp<Backend, EvtStateCats> MemOperation;
typedef Compiler<Operation, Backend_X86_64_ObsLog> CompilerT;
typedef RandInstTest<std::mt19937, RandomFactory> RIT;
typedef simplega::GenePool<RIT> GenePool;

static const size_t MAX_CODE_SIZE = 4096 * 16;

// Code base IP for each thread: distinct regions so IPs don't collide
static types::InstPtr thread_code_base(types::Pid pid) {
    return 0x200000000ULL + static_cast<uint64_t>(pid) * 0x100000ULL;
}

// ---- Serialization: write a single Op descriptor ----
struct OpDesc {
    uint16_t pid;
    uint8_t  op_type;  // 0=Read,1=ReadAddrDp,2=Write,3=RMW,4=CacheFlush,5=Delay,6=Return
    uint64_t addr;
};

static OpDesc describe_op(const typename Operation::Ptr& op) {
    OpDesc d;
    d.pid = op->pid();
    d.op_type = 255;
    d.addr = 0;

    if (dynamic_cast<strong::ReadModifyWrite*>(op.get())) {
        d.op_type = 3;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<strong::Write*>(op.get())) {
        d.op_type = 2;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<strong::ReadAddrDp*>(op.get())) {
        d.op_type = 1;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<strong::Read*>(op.get())) {
        d.op_type = 0;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<strong::CacheFlush*>(op.get())) {
        d.op_type = 4;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<strong::Delay*>(op.get())) {
        d.op_type = 5;
    } else if (dynamic_cast<strong::Return*>(op.get())) {
        d.op_type = 6;
    }
    return d;
}

// ---- Serialize a single RIT genome to file ----
static void write_rit_genome(FILE* f, const RIT& rit) {
    uint32_t genome_size = static_cast<uint32_t>(rit.get().size());
    fwrite(&genome_size, sizeof(genome_size), 1, f);

    for (size_t i = 0; i < rit.get().size(); ++i) {
        OpDesc d = describe_op(rit.get()[i]);
        fwrite(&d.pid, sizeof(d.pid), 1, f);
        fwrite(&d.op_type, sizeof(d.op_type), 1, f);
        fwrite(&d.addr, sizeof(d.addr), 1, f);
    }

    float fitness = rit.Fitness();
    fwrite(&fitness, sizeof(fitness), 1, f);

    const auto& fa = rit.fitaddrs();
    uint32_t fa_count = static_cast<uint32_t>(fa.get().size());
    fwrite(&fa_count, sizeof(fa_count), 1, f);
    for (const auto& addr : fa.get()) {
        uint64_t a = addr;
        fwrite(&a, sizeof(a), 1, f);
    }
}

// ---- Serialize entire GA pool state ----
static bool write_pool_state(const std::string& path,
                             uint32_t num_threads, uint32_t ops_per_thread,
                             uint64_t test_mem_addr, uint64_t test_mem_bytes,
                             uint64_t test_mem_stride, uint64_t rng_seed,
                             const GenePool& pool, uint32_t active_index) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); return false; }

    fwrite(&num_threads, sizeof(num_threads), 1, f);
    fwrite(&ops_per_thread, sizeof(ops_per_thread), 1, f);
    fwrite(&test_mem_addr, sizeof(test_mem_addr), 1, f);
    fwrite(&test_mem_bytes, sizeof(test_mem_bytes), 1, f);
    fwrite(&test_mem_stride, sizeof(test_mem_stride), 1, f);
    fwrite(&rng_seed, sizeof(rng_seed), 1, f);

    float mr = pool.mutation_rate();
    fwrite(&mr, sizeof(mr), 1, f);

    uint32_t pop_size = static_cast<uint32_t>(pool.population_size());
    fwrite(&pop_size, sizeof(pop_size), 1, f);

    uint32_t steps = static_cast<uint32_t>(pool.steps());
    fwrite(&steps, sizeof(steps), 1, f);

    for (const auto& rit : pool.get()) {
        write_rit_genome(f, rit);
    }

    fwrite(&active_index, sizeof(active_index), 1, f);

    fclose(f);
    return true;
}

int main(int argc, char* argv[]) {
    if (argc < 7) {
        fprintf(stderr,
                "Usage: %s <num_threads> <ops_per_thread> <test_mem_bytes> "
                "<test_mem_stride> <test_mem_addr> <output_dir> "
                "[--population <N>] [--mutation_rate <F>]\n",
                argv[0]);
        return 1;
    }

    const uint32_t num_threads     = static_cast<uint32_t>(strtoull(argv[1], nullptr, 0));
    const uint32_t ops_per_thread  = static_cast<uint32_t>(strtoull(argv[2], nullptr, 0));
    const uint64_t test_mem_bytes  = strtoull(argv[3], nullptr, 0);
    const uint64_t test_mem_stride = strtoull(argv[4], nullptr, 0);
    const uint64_t test_mem_addr   = strtoull(argv[5], nullptr, 0);
    const std::string output_dir   = argv[6];

    uint32_t population_size = 10;
    float mutation_rate = 0.02f;
    for (int i = 7; i < argc; i++) {
        if (strcmp(argv[i], "--population") == 0 && i + 1 < argc) {
            population_size = static_cast<uint32_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--mutation_rate") == 0 && i + 1 < argc) {
            mutation_rate = static_cast<float>(atof(argv[++i]));
        }
    }

    printf("=== Host Generate (Initial Round) [ObsLog Mode] ===\n");
    printf("Threads: %u, Ops/thread: %u\n", num_threads, ops_per_thread);
    printf("Test memory: %llu bytes, stride=0x%llx, addr=0x%llx\n",
           (unsigned long long)test_mem_bytes,
           (unsigned long long)test_mem_stride,
           (unsigned long long)test_mem_addr);
    printf("Population: %u, Mutation rate: %.3f\n", population_size, mutation_rate);
    printf("Output dir: %s\n", output_dir.c_str());

    // ---- Setup ----
    types::Addr min_addr = test_mem_addr;
    types::Addr max_addr = test_mem_addr + test_mem_bytes - 1;

    RandomFactory factory(
        0,
        static_cast<types::Pid>(num_threads - 1),
        min_addr,
        max_addr,
        test_mem_stride
    );

    std::random_device rd;
    uint64_t rng_seed = rd();
    std::mt19937 urng(static_cast<unsigned>(rng_seed));

    // ---- Create initial GA population ----
    printf("Creating initial population of %u individuals...\n", population_size);

    GenePool::Population initial_population;
    for (uint32_t i = 0; i < population_size; ++i) {
        initial_population.emplace_back(urng, &factory,
                                        ops_per_thread * num_threads);
    }

    GenePool pool(std::move(initial_population), mutation_rate);
    printf("Population initialized (size=%zu)\n", pool.population_size());

    // ---- Pick the first individual and emit code ----
    uint32_t active_index = 0;
    RIT active_rit = *pool.get().begin();

    printf("Emitting code for individual #%u...\n", active_index);

    {
        cats::ExecWitness ew;
        cats::Arch_TSO arch;
        auto evts = std::make_unique<EvtStateCats>(&ew, &arch);

        auto threads = active_rit.threads();
        CompilerT compiler(std::move(evts), std::move(threads));

        struct ThreadInfo {
            types::Pid pid;
            types::InstPtr base_ip;
            std::vector<char> code;
            size_t code_size;
            uint32_t obs_log_count;  // number of Read observations in this thread
        };
        std::vector<ThreadInfo> thread_infos;

        for (types::Pid pid = 0; pid < static_cast<types::Pid>(num_threads); ++pid) {
            ThreadInfo ti;
            ti.pid = pid;
            ti.base_ip = thread_code_base(pid);
            ti.code.resize(MAX_CODE_SIZE, 0);

            // Reset backend's obs_log counter before each thread emit
            // Note: Compiler owns the backend; we access it via the emitted output.
            // Actually the backend is inside the Compiler and resets per-Emit.
            // We need to track obs_log_count per thread.
            // The backend inside Compiler is shared across threads, so we need
            // to read the count before and after each thread's Emit.
            // But the backend is private inside Compiler... We need a different approach.
            //
            // Alternative: count Read/ReadAddrDp/RMW ops per thread from the genome.
            // This is simpler and avoids needing to expose the backend.

            ti.code_size = compiler.Emit(pid, ti.base_ip, ti.code.data(), MAX_CODE_SIZE);

            if (ti.code_size == 0) {
                fprintf(stderr, "Warning: thread %d generated 0 bytes of code\n", pid);
            }

            // Count Read observations for this thread from the genome
            ti.obs_log_count = 0;
            for (const auto& op : active_rit.get()) {
                if (op->pid() != pid) continue;
                if (dynamic_cast<strong::ReadModifyWrite*>(op.get()) ||
                    dynamic_cast<strong::ReadAddrDp*>(op.get()) ||
                    dynamic_cast<strong::Read*>(op.get())) {
                    // Write inherits from Read, so check RMW and Write first
                    if (!dynamic_cast<strong::Write*>(op.get()) ||
                        dynamic_cast<strong::ReadModifyWrite*>(op.get())) {
                        ti.obs_log_count++;
                    }
                }
            }

            // Append 'ret'
            Backend_X86_64_ObsLog backend;
            size_t ret_size = backend.Return(ti.code.data() + ti.code_size,
                                             MAX_CODE_SIZE - ti.code_size);
            ti.code_size += ret_size;

            thread_infos.push_back(ti);
            printf("  Thread %d: %zu bytes, base_ip=0x%llx, obs_log_entries=%u\n",
                   pid, ti.code_size, (unsigned long long)ti.base_ip,
                   ti.obs_log_count);
        }

        // Write per-thread code files
        for (const auto& ti : thread_infos) {
            char fname[512];
            snprintf(fname, sizeof(fname), "%s/thread_%d.bin",
                     output_dir.c_str(), ti.pid);
            FILE* f = fopen(fname, "wb");
            if (!f) { perror(fname); return 1; }
            fwrite(ti.code.data(), 1, ti.code_size, f);
            fclose(f);
            printf("  Wrote %s\n", fname);
        }

        // Write meta.bin
        // Format:
        //   [uint32_t] num_threads
        //   [uint64_t] test_mem_addr
        //   [uint64_t] test_mem_bytes
        //   [uint64_t] test_mem_stride
        //   For each thread:
        //     [uint32_t] code_size
        //     [uint64_t] base_ip
        //     [uint32_t] obs_log_count   <-- NEW: number of obs log entries
        {
            std::string meta_path = output_dir + "/meta.bin";
            FILE* f = fopen(meta_path.c_str(), "wb");
            if (!f) { perror("meta.bin"); return 1; }

            uint32_t nt = num_threads;
            fwrite(&nt, sizeof(nt), 1, f);

            uint64_t val;
            val = test_mem_addr;   fwrite(&val, sizeof(val), 1, f);
            val = test_mem_bytes;  fwrite(&val, sizeof(val), 1, f);
            val = test_mem_stride; fwrite(&val, sizeof(val), 1, f);

            for (const auto& ti : thread_infos) {
                uint32_t cs = static_cast<uint32_t>(ti.code_size);
                fwrite(&cs, sizeof(cs), 1, f);
                uint64_t bip = ti.base_ip;
                fwrite(&bip, sizeof(bip), 1, f);
                uint32_t olc = ti.obs_log_count;
                fwrite(&olc, sizeof(olc), 1, f);
            }

            fclose(f);
            printf("  Wrote %s\n", meta_path.c_str());
        }
    }

    // ---- Save GA pool state ----
    {
        std::string pool_path = output_dir + "/pool_state.bin";
        if (!write_pool_state(pool_path, num_threads, ops_per_thread,
                              test_mem_addr, test_mem_bytes, test_mem_stride,
                              rng_seed, pool, active_index)) {
            return 1;
        }
        printf("  Wrote %s\n", pool_path.c_str());
    }

    printf("\n=== Initial generation complete ===\n");
    printf("Next step: copy thread_*.bin and meta.bin to QEMU guest, run guest_runner\n");
    printf("Then run: host_verify %s\n", output_dir.c_str());
    return 0;
}
