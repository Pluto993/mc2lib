/*
 * host_verify.cpp
 *
 * Host-side verification + GA iteration + next-round generation.
 * Uses observation log (obs_log.bin) for correct per-Read UpdateObs.
 *
 * KEY CHANGE (Observation Log):
 *   Previous version tried to use the final memory state to reconstruct
 *   rf/co relations -- this was WRONG because we could only see each address's
 *   last written value, not what intermediate Reads actually observed.
 *
 *   Now we use obs_log.bin, which records the exact WriteID that each Read
 *   operation observed at execution time. This allows us to:
 *     - Call Read::UpdateObs with the correct observed WriteID → builds rf
 *     - Call Write::UpdateObs with the correct previous WriteID → builds co
 *     - Call RMW::UpdateObs with the correct old value → builds rf+co
 *     - Then run Checker::valid_exec() on a properly constructed ExecWitness
 *
 * Build on host (x86_64):
 *   g++ -std=c++14 -O2 -I../../../include host_verify.cpp -o host_verify
 *
 * Usage:
 *   ./host_verify <shared_dir>
 *
 * Input:
 *   <shared_dir>/pool_state.bin    -- GA population state
 *   <shared_dir>/meta.bin          -- metadata (with obs_log_count)
 *   <shared_dir>/test_result.bin   -- final memory dump from guest_runner
 *   <shared_dir>/obs_log.bin       -- per-Read observation log from guest_runner
 *
 * Output (overwrites):
 *   <shared_dir>/thread_*.bin      -- new per-thread machine code
 *   <shared_dir>/meta.bin          -- updated metadata
 *   <shared_dir>/pool_state.bin    -- updated GA population state
 *
 * Exit code:
 *   0 = pass (no violation), next round generated
 *   1 = memory consistency violation detected, next round generated
 *   2 = error
 */

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <list>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <unordered_set>
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

// ---- Type aliases (must match host_generate.cpp) ----
// Operation/MemOperation use abstract strong::Backend (matching Op subclasses).
// CompilerT uses concrete Backend_X86_64_ObsLog (Compiler needs a value member).
typedef Op<Backend, EvtStateCats> Operation;
typedef MemOp<Backend, EvtStateCats> MemOperation;
typedef Compiler<Operation, Backend_X86_64_ObsLog> CompilerT;
typedef RandInstTest<std::mt19937, RandomFactory> RIT;
typedef simplega::GenePool<RIT> GenePool;

static const size_t MAX_CODE_SIZE = 4096 * 16;

static types::InstPtr thread_code_base(types::Pid pid) {
    return 0x200000000ULL + static_cast<uint64_t>(pid) * 0x100000ULL;
}

// ---- Op descriptor (must match host_generate.cpp) ----
struct OpDesc {
    uint16_t pid;
    uint8_t  op_type;
    uint64_t addr;
};

// ---- Reconstruct Operation::Ptr from descriptor ----
static typename Operation::Ptr make_op(const OpDesc& d) {
    switch (d.op_type) {
        case 0:  return std::make_shared<Read>(d.addr, d.pid);
        case 1:  return std::make_shared<ReadAddrDp>(d.addr, d.pid);
        case 2:  return std::make_shared<Write>(d.addr, d.pid);
        case 3:  return std::make_shared<ReadModifyWrite>(d.addr, d.pid);
        case 4:  return std::make_shared<CacheFlush>(d.addr, d.pid);
        case 5:  return std::make_shared<Delay>(1, d.pid);
        case 6:  return std::make_shared<Return>(d.pid);
        default:
            fprintf(stderr, "Unknown op_type %d\n", d.op_type);
            return std::make_shared<Return>(d.pid);
    }
}

// ---- Describe an Op for serialization ----
static OpDesc describe_op(const typename Operation::Ptr& op) {
    OpDesc d;
    d.pid = op->pid();
    d.op_type = 255;
    d.addr = 0;

    if (dynamic_cast<ReadModifyWrite*>(op.get())) {
        d.op_type = 3;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<Write*>(op.get())) {
        d.op_type = 2;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<ReadAddrDp*>(op.get())) {
        d.op_type = 1;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<Read*>(op.get())) {
        d.op_type = 0;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<CacheFlush*>(op.get())) {
        d.op_type = 4;
        d.addr = dynamic_cast<MemOperation*>(op.get())->addr();
    } else if (dynamic_cast<Delay*>(op.get())) {
        d.op_type = 5;
    } else if (dynamic_cast<Return*>(op.get())) {
        d.op_type = 6;
    }
    return d;
}

// ---- Read a single RIT genome from file ----
static RIT read_rit_genome(FILE* f, std::mt19937& urng, const RandomFactory* factory) {
    uint32_t genome_size;
    fread(&genome_size, sizeof(genome_size), 1, f);

    std::vector<typename Operation::Ptr> genome(genome_size);
    for (uint32_t i = 0; i < genome_size; i++) {
        OpDesc d;
        fread(&d.pid, sizeof(d.pid), 1, f);
        fread(&d.op_type, sizeof(d.op_type), 1, f);
        fread(&d.addr, sizeof(d.addr), 1, f);
        genome[i] = make_op(d);
    }

    float fitness;
    fread(&fitness, sizeof(fitness), 1, f);

    uint32_t fa_count;
    fread(&fa_count, sizeof(fa_count), 1, f);

    RIT rit(urng, factory, genome_size);
    auto* gptr = rit.get_ptr();
    *gptr = std::move(genome);

    rit.set_fitness(fitness);

    for (uint32_t i = 0; i < fa_count; i++) {
        uint64_t a;
        fread(&a, sizeof(a), 1, f);
        rit.fitaddrsptr()->Insert(static_cast<types::Addr>(a));
    }

    return rit;
}

// ---- Write a single RIT genome to file ----
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

// ---- Pool state structure ----
struct PoolState {
    uint32_t num_threads;
    uint32_t ops_per_thread;
    uint64_t test_mem_addr;
    uint64_t test_mem_bytes;
    uint64_t test_mem_stride;
    uint64_t rng_seed;
    float    mutation_rate;
    uint32_t population_size;
    uint32_t ga_steps;
    uint32_t active_index;
};

static bool read_pool_state(const std::string& path, PoolState& ps,
                            GenePool::Population& population,
                            std::mt19937& urng, const RandomFactory* factory) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path.c_str()); return false; }

    fread(&ps.num_threads, sizeof(ps.num_threads), 1, f);
    fread(&ps.ops_per_thread, sizeof(ps.ops_per_thread), 1, f);
    fread(&ps.test_mem_addr, sizeof(ps.test_mem_addr), 1, f);
    fread(&ps.test_mem_bytes, sizeof(ps.test_mem_bytes), 1, f);
    fread(&ps.test_mem_stride, sizeof(ps.test_mem_stride), 1, f);
    fread(&ps.rng_seed, sizeof(ps.rng_seed), 1, f);
    fread(&ps.mutation_rate, sizeof(ps.mutation_rate), 1, f);
    fread(&ps.population_size, sizeof(ps.population_size), 1, f);
    fread(&ps.ga_steps, sizeof(ps.ga_steps), 1, f);

    population.clear();
    for (uint32_t i = 0; i < ps.population_size; i++) {
        population.push_back(read_rit_genome(f, urng, factory));
    }

    fread(&ps.active_index, sizeof(ps.active_index), 1, f);

    fclose(f);
    return true;
}

static bool write_pool_state(const std::string& path, const PoolState& ps,
                             const GenePool& pool, uint32_t active_index) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); return false; }

    fwrite(&ps.num_threads, sizeof(ps.num_threads), 1, f);
    fwrite(&ps.ops_per_thread, sizeof(ps.ops_per_thread), 1, f);
    fwrite(&ps.test_mem_addr, sizeof(ps.test_mem_addr), 1, f);
    fwrite(&ps.test_mem_bytes, sizeof(ps.test_mem_bytes), 1, f);
    fwrite(&ps.test_mem_stride, sizeof(ps.test_mem_stride), 1, f);
    fwrite(&ps.rng_seed, sizeof(ps.rng_seed), 1, f);

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

// ---- Read binary file into buffer ----
static std::vector<uint8_t> read_file_bin(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s: %s\n", path.c_str(), strerror(errno));
        return {};
    }
    fseek(f, 0, SEEK_END);
    size_t size = ftell(f);
    fseek(f, 0, SEEK_SET);
    std::vector<uint8_t> buf(size);
    fread(buf.data(), 1, size, f);
    fclose(f);
    return buf;
}

// ---- Per-thread observation log data ----
struct ThreadObsLog {
    uint32_t count;
    std::vector<types::WriteID> entries;
};

// ---- Read obs_log.bin ----
static bool read_obs_log(const std::string& path,
                         uint32_t num_threads,
                         std::vector<ThreadObsLog>& logs) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        fprintf(stderr, "Cannot open %s: %s\n", path.c_str(), strerror(errno));
        return false;
    }

    logs.resize(num_threads);
    for (uint32_t i = 0; i < num_threads; i++) {
        fread(&logs[i].count, sizeof(uint32_t), 1, f);
        logs[i].entries.resize(logs[i].count);
        if (logs[i].count > 0) {
            fread(logs[i].entries.data(), sizeof(types::WriteID),
                  logs[i].count, f);
        }
    }

    fclose(f);
    return true;
}

// ====================================================================
// Helper: Determine if an Operation is a "Read-like" op (needs obs log)
// Write inherits from Read in the class hierarchy, so we must be careful.
// Read-like ops: Read, ReadAddrDp, ReadModifyWrite (but NOT Write)
// ====================================================================
static bool is_read_like(const typename Operation::Ptr& op) {
    if (dynamic_cast<ReadModifyWrite*>(op.get())) return true;
    if (dynamic_cast<Write*>(op.get())) return false;  // Write inherits Read, exclude it
    if (dynamic_cast<ReadAddrDp*>(op.get())) return true;
    if (dynamic_cast<Read*>(op.get())) return true;
    return false;
}

// ====================================================================
// Helper: Determine if an Operation is a "Write-like" op (needs final mem obs)
// Write-like ops: Write (but not Read, ReadAddrDp)
// Note: ReadModifyWrite is handled separately (both read and write parts)
// ====================================================================
static bool is_write_only(const typename Operation::Ptr& op) {
    if (dynamic_cast<ReadModifyWrite*>(op.get())) return false;
    if (dynamic_cast<Write*>(op.get())) return true;
    return false;
}

// ====================================================================
// Helper: Compute the at_ offset from the Op's base IP (key_ip).
//
// During Emit, Backend sets *at = start + offset, where offset depends on
// the instruction encoding. UpdateObs asserts ip == at_. We need to know
// this offset so we can call UpdateObs(key_ip + at_offset, ...).
//
// The offset depends on:
//   1. Op type (Read, Write, ReadAddrDp, ReadModifyWrite, etc.)
//   2. Address range (addr <= 0xFFFFFFFF uses shorter encoding)
//   3. sizeof(WriteID)
//
// These offsets are determined by the x86_64 Backend's instruction encoding.
// See x86_64.hpp for the exact encodings.
// ====================================================================
static size_t compute_at_offset(Operation* op) {
    auto* mem_op = dynamic_cast<MemOperation*>(op);
    if (mem_op == nullptr) return 0;

    types::Addr addr = mem_op->addr();
    bool is_high = (addr > static_cast<types::Addr>(0xffffffff));

    if (dynamic_cast<ReadModifyWrite*>(op)) {
        if (!is_high) {
            // mov write_id, %al (2B) + mov addr, %edx (5B) + lock xchg (3B)
            // at_ = start + 0x7
            return 0x7;
        } else {
            switch (sizeof(types::WriteID)) {
                case 1: return 0xc;  // mov %al (2B) + movabs %rdx (10B) + lock xchg (3B), at_=start+0xc
                case 2: return 0xf;
                default: return 0;
            }
        }
    } else if (dynamic_cast<Write*>(op)) {
        if (!is_high) {
            // movb write_id, addr(4 bytes): at_ = start
            return 0;
        } else {
            switch (sizeof(types::WriteID)) {
                case 1: return 0xa;  // movabs addr, %rax (10B) + movb (%rax) (3B), at_=start+0xa
                case 2: return 0xf;
                default: return 0;
            }
        }
    } else if (dynamic_cast<ReadAddrDp*>(op)) {
        if (!is_high) {
            // xor (3B) + movzbl addr(%rax) (7B): at_ = start + 3
            return 3;
        } else {
            // xor (3B) + movabs (10B) + add (3B) + movzbl (3B): at_ = start + 0x10
            return 0x10;
        }
    } else if (dynamic_cast<Read*>(op)) {
        // Read: at_ = start for all encodings
        return 0;
    } else if (dynamic_cast<CacheFlush*>(op)) {
        return 0;
    }

    return 0;
}

// ====================================================================
// MAIN
// ====================================================================
int main(int argc, char* argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <shared_dir>\n", argv[0]);
        return 2;
    }

    const std::string shared_dir = argv[1];

    printf("=== Host Verify + Next-Round Generate [ObsLog Mode] ===\n");

    // ---- 1. Load pool state ----
    PoolState ps;
    std::random_device rd;
    uint64_t new_seed = rd();
    std::mt19937 urng(static_cast<unsigned>(new_seed));

    {
        FILE* f = fopen((shared_dir + "/pool_state.bin").c_str(), "rb");
        if (!f) {
            fprintf(stderr, "Cannot open pool_state.bin\n");
            return 2;
        }
        fread(&ps.num_threads, sizeof(ps.num_threads), 1, f);
        fread(&ps.ops_per_thread, sizeof(ps.ops_per_thread), 1, f);
        fread(&ps.test_mem_addr, sizeof(ps.test_mem_addr), 1, f);
        fread(&ps.test_mem_bytes, sizeof(ps.test_mem_bytes), 1, f);
        fread(&ps.test_mem_stride, sizeof(ps.test_mem_stride), 1, f);
        fclose(f);
    }

    types::Addr min_addr = ps.test_mem_addr;
    types::Addr max_addr = ps.test_mem_addr + ps.test_mem_bytes - 1;

    RandomFactory factory(
        0,
        static_cast<types::Pid>(ps.num_threads - 1),
        min_addr,
        max_addr,
        ps.test_mem_stride
    );

    GenePool::Population population;
    if (!read_pool_state(shared_dir + "/pool_state.bin", ps, population,
                         urng, &factory)) {
        return 2;
    }

    printf("Loaded pool: %u individuals, %u steps completed, active=#%u\n",
           ps.population_size, ps.ga_steps, ps.active_index);
    printf("Params: threads=%u ops/thread=%u mem=0x%llx+%llu stride=0x%llx\n",
           ps.num_threads, ps.ops_per_thread,
           (unsigned long long)ps.test_mem_addr,
           (unsigned long long)ps.test_mem_bytes,
           (unsigned long long)ps.test_mem_stride);

    // ---- 2. Read test_result.bin (final memory state, for Write co relations) ----
    auto result_data = read_file_bin(shared_dir + "/test_result.bin");
    if (result_data.size() != ps.test_mem_bytes) {
        fprintf(stderr, "test_result.bin size mismatch: got %zu, expected %llu\n",
                result_data.size(), (unsigned long long)ps.test_mem_bytes);
        return 2;
    }

    // ---- 3. Read obs_log.bin (per-Read observation log) ----
    std::vector<ThreadObsLog> obs_logs;
    if (!read_obs_log(shared_dir + "/obs_log.bin", ps.num_threads, obs_logs)) {
        return 2;
    }

    for (uint32_t i = 0; i < ps.num_threads; i++) {
        printf("Thread %u: %u obs_log entries loaded\n", i, obs_logs[i].count);
    }

    // ---- 4. Reconstruct Compiler for the active individual ----
    auto active_it = population.begin();
    std::advance(active_it, ps.active_index);
    RIT& active_rit = *active_it;

    printf("\nReconstructing Compiler for active individual #%u...\n", ps.active_index);

    cats::ExecWitness ew;
    cats::Arch_TSO arch;
    auto evts = std::make_unique<EvtStateCats>(&ew, &arch);

    auto threads = active_rit.threads();
CompilerT compiler(std::move(evts), std::move(threads));

    // Re-emit code to rebuild IP-to-Op mapping (same as host_generate did)
    struct ThreadEmitInfo {
        types::Pid pid;
        types::InstPtr base_ip;
        size_t code_size;
    };
    std::vector<ThreadEmitInfo> emit_infos;

    for (types::Pid pid = 0; pid < static_cast<types::Pid>(ps.num_threads); ++pid) {
        ThreadEmitInfo tei;
        tei.pid = pid;
        tei.base_ip = thread_code_base(pid);

        std::vector<char> code(MAX_CODE_SIZE, 0);
        tei.code_size = compiler.Emit(pid, tei.base_ip, code.data(), MAX_CODE_SIZE);

        emit_infos.push_back(tei);
        printf("  Thread %d: re-emitted %zu bytes, base_ip=0x%llx\n",
               pid, tei.code_size, (unsigned long long)tei.base_ip);
    }

    // ---- 5. Walk ops in IP order via ForEachOp, call UpdateObs with correct at_ ----
    //
    // 核心逻辑：
    //   Compiler::ForEachOp 按 IP 顺序遍历 ip_to_op_ 中的所有条目。
    //   每个条目包含 (key_ip, end_ip, Op*)，其中 key_ip 是 Op emit 时的
    //   start 参数。
    //
    //   但 UpdateObs 断言 ip == at_，而 at_ = key_ip + at_offset。
    //   at_offset 只取决于 Op 类型和地址范围（由 compute_at_offset 计算）。
    //
    //   对于每个 Op，我们用 key_ip + compute_at_offset(op) 作为传给
    //   UpdateObs 的 IP，这样恰好等于 at_，断言通过。
    //
    //   obs_log 的消费顺序：按线程内的程序顺序。ForEachOp 按全局 IP 顺序
    //   遍历，但同一线程的 Ops 在 IP 空间中是连续的（因为 thread_code_base
    //   为每个线程分配了不重叠的 IP 区间），所以线程内的遍历顺序就是程序顺序。
    //
    printf("Processing observations with obs_log...\n");

    const uint8_t* final_mem = result_data.data();
    uint64_t base_addr = ps.test_mem_addr;
    size_t obs_count = 0;
    size_t obs_errors = 0;

    // Per-thread obs_log cursor
    std::vector<uint32_t> obs_cursor(ps.num_threads, 0);

    compiler.ForEachOp([&](types::InstPtr key_ip, types::InstPtr end_ip, Operation* op) {
        auto* mem_op = dynamic_cast<MemOperation*>(op);
        if (mem_op == nullptr) return;  // skip Delay, Return, etc.

        types::Addr addr = mem_op->addr();
        size_t at_off = compute_at_offset(op);
        types::InstPtr at_ip = key_ip + at_off;

        try {
            if (dynamic_cast<ReadModifyWrite*>(op)) {
                // RMW: obs_log has the old value (what xchg read)
                uint32_t tid = op->pid();
                assert(obs_cursor[tid] < obs_logs[tid].count);
                types::WriteID obs_val = obs_logs[tid].entries[obs_cursor[tid]++];

                // part=0: Read part (rf relation)
                compiler.UpdateObs(at_ip, 0, addr, &obs_val,
                                   sizeof(types::WriteID));
                // part=1: Write part (co relation)
                compiler.UpdateObs(at_ip, 1, addr, &obs_val,
                                   sizeof(types::WriteID));
                obs_count++;
            } else if (dynamic_cast<Write*>(op)) {
                // Plain Write: use final memory value for co relation
                if (addr >= base_addr && addr < base_addr + ps.test_mem_bytes) {
                    size_t offset = static_cast<size_t>(addr - base_addr);
                    const types::WriteID* from_id =
                        reinterpret_cast<const types::WriteID*>(final_mem + offset);
                    compiler.UpdateObs(at_ip, 0, addr, from_id,
                                       sizeof(types::WriteID));
                }
                obs_count++;
            } else if (dynamic_cast<ReadAddrDp*>(op) ||
                       dynamic_cast<Read*>(op)) {
                // Read / ReadAddrDp: use obs_log observed value
                uint32_t tid = op->pid();
                assert(obs_cursor[tid] < obs_logs[tid].count);
                types::WriteID obs_val = obs_logs[tid].entries[obs_cursor[tid]++];

                compiler.UpdateObs(at_ip, 0, addr, &obs_val,
                                   sizeof(types::WriteID));
                obs_count++;
            } else if (dynamic_cast<CacheFlush*>(op)) {
                // CacheFlush::UpdateObs does nothing
                types::WriteID dummy = 0;
                compiler.UpdateObs(at_ip, 0, addr, &dummy,
                                   sizeof(types::WriteID));
            }
        } catch (const mc::Error& e) {
            fprintf(stderr, "  UpdateObs error at IP=0x%llx addr=0x%llx: %s\n",
                    (unsigned long long)at_ip,
                    (unsigned long long)addr,
                    e.what());
            obs_errors++;
        } catch (const std::exception& e) {
            fprintf(stderr, "  UpdateObs exception at IP=0x%llx: %s\n",
                    (unsigned long long)at_ip, e.what());
            obs_errors++;
        }
    });

    printf("Observations: %zu processed, %zu errors\n", obs_count, obs_errors);

    // Verify obs_log cursors were fully consumed
    for (uint32_t i = 0; i < ps.num_threads; i++) {
        if (obs_cursor[i] != obs_logs[i].count) {
            fprintf(stderr, "Warning: thread %u obs_log cursor at %u/%u "
                    "(not fully consumed)\n",
                    i, obs_cursor[i], obs_logs[i].count);
        }
    }

    // ---- 6. Run consistency checker ----
    printf("Running consistency check (TSO model)...\n");

    cats::ArchProxy<cats::Arch_TSO> arch_proxy(&arch);
    arch_proxy.Memoize(ew);
    auto checker = arch_proxy.MakeChecker(&ew);

    bool violation_found = false;
    EventRel::Path cyclic_path;

    RIT::AddrSet violation_addrs;

    try {
        checker->valid_exec(&cyclic_path);
        printf("RESULT: No memory consistency violation detected.\n");
    } catch (const mc::Error& e) {
        violation_found = true;
        printf("RESULT: *** MEMORY CONSISTENCY VIOLATION: %s ***\n", e.what());

        if (!cyclic_path.empty()) {
            printf("  Cyclic path (%zu events):\n", cyclic_path.size());
            for (const auto& event : cyclic_path) {
                printf("    %s\n", static_cast<std::string>(event).c_str());
                violation_addrs.Insert(event.addr);
            }
        }
    }

    // ---- 7. Compute fitness and update active individual ----
    //
    // mc2lib 原始逻辑：
    //   - 如果发现 violation，fitness = violation 中涉及的唯一地址数
    //   - fitaddrs = violation 涉及的地址集合
    //   - CrossoverMutate 会偏好保留访问 fitaddrs 中地址的 Op
    //
    // 没有 violation 时，使用地址覆盖率作为低权重 fitness
    float fitness = static_cast<float>(violation_addrs.get().size());

    if (!violation_found) {
        // 没有 violation 时，使用地址覆盖率作为低权重 fitness
        std::unordered_set<types::Addr> observed_addrs;
        for (const auto& op_ptr : active_rit.get()) {
            auto* mop = dynamic_cast<MemOperation*>(op_ptr.get());
            if (mop) observed_addrs.insert(mop->addr());
        }
        fitness = static_cast<float>(observed_addrs.size()) * 0.01f;
    }

    active_rit.set_fitness(fitness);

    auto* fa_ptr = active_rit.fitaddrsptr();
    for (const auto& a : violation_addrs.get()) {
        fa_ptr->Insert(a);
    }

    printf("Fitness for individual #%u: %.2f (fitaddrs: %zu)\n",
           ps.active_index, fitness, active_rit.fitaddrs().get().size());

    // ---- 8. GA Evolution Step ----
    printf("\nRunning GA evolution step...\n");

    GenePool pool(std::move(population), ps.mutation_rate);

    mcversi::CrossoverMutate<std::mt19937, RIT, MemOperation>
        crossover_mutate(0.2, 0.05);

    size_t select_count = std::max<size_t>(3, pool.population_size() / 2);
    if (select_count > pool.population_size()) {
        select_count = pool.population_size();
    }

    auto selection = pool.SelectRoulette(urng, select_count);
    pool.SelectionSort(&selection);

    size_t keep = 1;
    pool.Step(urng, crossover_mutate, selection, selection.size(), keep);

    printf("GA step complete. Population: %zu, Steps: %zu\n",
           pool.population_size(), pool.steps());
    printf("Best fitness: %.2f, Average: %.2f\n",
           pool.BestFitness(), pool.AverageFitness());

    // ---- 9. Pick the next individual to test ----
    uint32_t new_active_index = (ps.active_index + 1) % pool.population_size();

    auto next_it = pool.get().begin();
    std::advance(next_it, new_active_index);
    RIT next_rit = *next_it;

    printf("\nEmitting code for next individual #%u...\n", new_active_index);

    // ---- 10. Emit new test code (with ObsLog backend) ----
    {
        cats::ExecWitness ew2;
        cats::Arch_TSO arch2;
        auto evts2 = std::make_unique<EvtStateCats>(&ew2, &arch2);

        auto new_threads = next_rit.threads();
CompilerT new_compiler(std::move(evts2), std::move(new_threads));

        struct ThreadInfo {
            types::Pid pid;
            types::InstPtr base_ip;
            std::vector<char> code;
            size_t code_size;
            uint32_t obs_log_count;
        };
        std::vector<ThreadInfo> thread_infos;

        for (types::Pid pid = 0; pid < static_cast<types::Pid>(ps.num_threads); ++pid) {
            ThreadInfo ti;
            ti.pid = pid;
            ti.base_ip = thread_code_base(pid);
            ti.code.resize(MAX_CODE_SIZE, 0);

            ti.code_size = new_compiler.Emit(pid, ti.base_ip, ti.code.data(), MAX_CODE_SIZE);

            if (ti.code_size == 0) {
                fprintf(stderr, "Warning: thread %d generated 0 bytes of code\n", pid);
            }

            // Count read-like ops for obs_log_count
            ti.obs_log_count = 0;
            for (const auto& op : next_rit.get()) {
                if (op->pid() != pid) continue;
                if (is_read_like(op)) {
                    ti.obs_log_count++;
                }
            }

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
                     shared_dir.c_str(), ti.pid);
            FILE* f = fopen(fname, "wb");
            if (!f) { perror(fname); return 2; }
            fwrite(ti.code.data(), 1, ti.code_size, f);
            fclose(f);
        }

        // Write meta.bin (new format with obs_log_count)
        {
            std::string meta_path = shared_dir + "/meta.bin";
            FILE* f = fopen(meta_path.c_str(), "wb");
            if (!f) { perror("meta.bin"); return 2; }

            uint32_t nt = ps.num_threads;
            fwrite(&nt, sizeof(nt), 1, f);

            uint64_t val;
            val = ps.test_mem_addr;   fwrite(&val, sizeof(val), 1, f);
            val = ps.test_mem_bytes;  fwrite(&val, sizeof(val), 1, f);
            val = ps.test_mem_stride; fwrite(&val, sizeof(val), 1, f);

            for (const auto& ti : thread_infos) {
                uint32_t cs = static_cast<uint32_t>(ti.code_size);
                fwrite(&cs, sizeof(cs), 1, f);
                uint64_t bip = ti.base_ip;
                fwrite(&bip, sizeof(bip), 1, f);
                uint32_t olc = ti.obs_log_count;
                fwrite(&olc, sizeof(olc), 1, f);
            }

            fclose(f);
        }
    }

    // ---- 11. Save updated pool state ----
    {
        PoolState new_ps = ps;
        new_ps.rng_seed = new_seed;

        std::string pool_path = shared_dir + "/pool_state.bin";
        if (!write_pool_state(pool_path, new_ps, pool, new_active_index)) {
            return 2;
        }
    }

    // ---- 12. Summary ----
    printf("\n=== Verification & Generation Summary ===\n");
    printf("  Round (GA step):   %zu\n", pool.steps());
    printf("  Events:            %zu\n", ew.events.size());
    printf("  PO edges:          %zu\n", ew.po.size());
    printf("  RF edges:          %zu\n", ew.rf.size());
    printf("  CO edges:          %zu\n", ew.co.size());
    printf("  Observations:      %zu\n", obs_count);
    printf("  Obs errors:        %zu\n", obs_errors);
    printf("  Violation:         %s\n", violation_found ? "YES" : "no");
    printf("  Next individual:   #%u\n", new_active_index);
    printf("\nNext step: copy thread_*.bin and meta.bin to QEMU guest, run guest_runner\n");
    printf("Then run:  host_verify %s\n", shared_dir.c_str());

    return violation_found ? 1 : 0;
}
