/*
 * x86_64_qemu.hpp
 *
 * Extended x86_64 Backend with observation logging support for QEMU.
 *
 * Problem:
 *   In gem5, the simulator intercepts every memory access at execution time
 *   and calls UpdateObs() with the actual observed WriteID. In QEMU, we only
 *   have the final memory state after all threads finish. We cannot know what
 *   each Read operation actually observed during execution.
 *
 * Solution (Observation Log):
 *   For each Read (Read / ReadAddrDp / ReadModifyWrite), after the original
 *   read instruction, we append an additional store instruction that saves
 *   the read value (%al for WriteID=uint8_t) to a dedicated "observation log"
 *   memory region. Each Read gets a unique slot in the log, addressed by a
 *   monotonically increasing index.
 *
 *   After execution, the observation log contains the exact WriteID that each
 *   Read saw at its execution moment. The host verifier reads this log and
 *   calls UpdateObs() with the correct per-Read observed values.
 *
 * Observation Log Layout:
 *   obs_log_base + 0 * sizeof(WriteID) : value read by 1st Read op
 *   obs_log_base + 1 * sizeof(WriteID) : value read by 2nd Read op
 *   obs_log_base + 2 * sizeof(WriteID) : value read by 3rd Read op
 *   ...
 *
 * Register Convention:
 *   - %rcx is reserved as the observation log pointer.
 *   - Before each thread's code executes, %rcx must be set to the
 *     thread's obs_log base address (by the guest_runner wrapper).
 *   - After each Read stores the observed value, %rcx is incremented
 *     by sizeof(WriteID).
 *   - The original code uses %rax/%rdx, so %rcx is safe to use.
 *
 * Copyright (c) 2024, Extended by QEMU adaptation
 * Based on mc2lib by Marco Elver (c) 2014-2016
 */

#ifndef MC2LIB_CODEGEN_OPS_X86_64_QEMU_HPP_
#define MC2LIB_CODEGEN_OPS_X86_64_QEMU_HPP_

#include "x86_64.hpp"

namespace mc2lib {
namespace codegen {
namespace strong {

/*
 * Backend_X86_64_ObsLog:
 *   继承原始 Backend_X86_64，重写 Read / ReadAddrDp / ReadModifyWrite，
 *   在每次读操作的机器码后面追加一条 store 指令，将读到的值（%al）
 *   保存到观测记录区（通过 %rcx 指针索引）。
 *
 * 约定：
 *   - %rcx = 观测记录区的当前写入指针（由 guest_runner 在线程入口处设置）
 *   - 每次 Read 后执行: mov %al, (%rcx); add $sizeof(WriteID), %rcx
 *   - 每次 RMW 后执行同样的操作（记录 xchg 之后 %al 中残留的"读到的旧值"）
 *
 * 注意：
 *   - RMW (lock xchg %al, (%rdx)) 执行后，%al 中保存的是原子交换前
 *     目标地址的旧值（即 Read 部分观测到的 WriteID）。新值已经写入了。
 *     所以直接保存 %al 就是正确的 Read observation。
 *   - obs_log_index_ 是一个编译期计数器，用于 host 端重建 IP→log_slot 映射。
 */
struct Backend_X86_64_ObsLog : Backend_X86_64 {

  Backend_X86_64_ObsLog() : obs_log_index_(0) {}

  void Reset() override {
    Backend_X86_64::Reset();
    obs_log_index_ = 0;
  }

  /*
   * 返回到目前为止已分配的观测记录槽数量。
   * host_generate / host_verify 通过这个值来确定观测记录区需要多大的空间，
   * 以及每个 Read 对应的 log slot 索引。
   */
  std::size_t obs_log_count() const { return obs_log_index_; }

  /* ---- Observation log store epilogue ----
   *
   * 在每个 Read 指令之后追加的机器码:
   *   mov %al, (%rcx)          ; 2 bytes: 88 01
   *   add $1, %rcx             ; 4 bytes: 48 83 c1 01
   * 共 6 字节 (sizeof(WriteID)==1 时)
   *
   * 如果 sizeof(WriteID)==2:
   *   mov %ax, (%rcx)          ; 3 bytes: 66 89 01
   *   add $2, %rcx             ; 4 bytes: 48 83 c1 02
   * 共 7 字节
   */
  static constexpr std::size_t kObsLogEpilogueLen =
      (sizeof(types::WriteID) == 1) ? 6 : 7;

  std::size_t EmitObsLogEpilogue(void *code, std::size_t len) const {
    char *cnext = static_cast<char *>(code);

    switch (sizeof(types::WriteID)) {
      case 1:
        // ASM> mov %al, (%rcx)    ; store observed WriteID to log
        assert(len >= 6);
        *cnext++ = 0x88;  // mov r8, r/m8
        *cnext++ = 0x01;  // ModRM: (%rcx)

        // ASM> add $1, %rcx       ; advance log pointer
        *cnext++ = 0x48;  // REX.W
        *cnext++ = 0x83;  // add r/m64, imm8
        *cnext++ = 0xc1;  // ModRM: %rcx
        *cnext++ = 0x01;  // imm8 = 1
        return 6;

      case 2:
        // ASM> mov %ax, (%rcx)    ; store observed WriteID to log
        assert(len >= 7);
        *cnext++ = 0x66;  // operand-size prefix
        *cnext++ = 0x89;  // mov r16, r/m16
        *cnext++ = 0x01;  // ModRM: (%rcx)

        // ASM> add $2, %rcx       ; advance log pointer
        *cnext++ = 0x48;  // REX.W
        *cnext++ = 0x83;  // add r/m64, imm8
        *cnext++ = 0xc1;  // ModRM: %rcx
        *cnext++ = 0x02;  // imm8 = 2
        return 7;

      default:
        throw std::logic_error("Unsupported WriteID size for obs log");
    }
  }

  /* ---- Override Read: original read + obs log store ---- */
  std::size_t Read(types::Addr addr, types::InstPtr start, void *code,
                   std::size_t len, types::InstPtr *at) const override {
    // Emit the original read instruction
    std::size_t read_len =
        Backend_X86_64::Read(addr, start, code, len, at);

    // Emit obs log epilogue after the read
    char *code_after = static_cast<char *>(code) + read_len;
    std::size_t log_len =
        const_cast<Backend_X86_64_ObsLog *>(this)->EmitObsLogEpilogue(
            code_after, len - read_len);

    // Track this read's obs_log slot
    const_cast<Backend_X86_64_ObsLog *>(this)->obs_log_index_++;

    return read_len + log_len;
  }

  /* ---- Override ReadAddrDp: original readaddrdp + obs log store ---- */
  std::size_t ReadAddrDp(types::Addr addr, types::InstPtr start, void *code,
                         std::size_t len, types::InstPtr *at) const override {
    // Emit the original read-with-address-dependency instruction
    std::size_t read_len =
        Backend_X86_64::ReadAddrDp(addr, start, code, len, at);

    // Emit obs log epilogue
    char *code_after = static_cast<char *>(code) + read_len;
    std::size_t log_len =
        const_cast<Backend_X86_64_ObsLog *>(this)->EmitObsLogEpilogue(
            code_after, len - read_len);

    const_cast<Backend_X86_64_ObsLog *>(this)->obs_log_index_++;

    return read_len + log_len;
  }

  /* ---- Override ReadModifyWrite: original RMW + obs log store ----
   *
   * lock xchg %al, (%rdx) 执行后：
   *   - (%rdx) 被写入了新的 write_id
   *   - %al 中保存的是之前 (%rdx) 的旧值（即 Read 观测到的 WriteID）
   *
   * 所以直接在 xchg 后面追加 obs log store 即可正确记录 Read observation。
   */
  std::size_t ReadModifyWrite(types::Addr addr, types::WriteID write_id,
                              types::InstPtr start, void *code,
                              std::size_t len,
                              types::InstPtr *at) const override {
    // Emit the original RMW instruction
    std::size_t rmw_len =
        Backend_X86_64::ReadModifyWrite(addr, write_id, start, code, len, at);

    // Emit obs log epilogue (saves the old value that was in %al after xchg)
    char *code_after = static_cast<char *>(code) + rmw_len;
    std::size_t log_len =
        const_cast<Backend_X86_64_ObsLog *>(this)->EmitObsLogEpilogue(
            code_after, len - rmw_len);

    const_cast<Backend_X86_64_ObsLog *>(this)->obs_log_index_++;

    return rmw_len + log_len;
  }

  /* Write 和 CacheFlush 不需要观测记录，保持原样 */
  /* Return 和 Delay 也不需要，继承自 Backend_X86_64 */

 private:
  std::size_t obs_log_index_;
};

}  // namespace strong
}  // namespace codegen
}  // namespace mc2lib

#endif /* MC2LIB_CODEGEN_OPS_X86_64_QEMU_HPP_ */
