/*
 * RISC-V backend for mc2lib
 * Implements memory operations for RISC-V architecture with RVWMO support
 * 
 * RISC-V Memory Model: RVWMO (RISC-V Weak Memory Ordering)
 * - Relaxed memory model (similar to ARMv7/8)
 * - Uses FENCE instructions for synchronization
 * - Supports various fence types: rw, r, w, i, o
 */

#ifndef MC2LIB_CODEGEN_OPS_RISCV_HPP_
#define MC2LIB_CODEGEN_OPS_RISCV_HPP_

#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>

#include "../cats.hpp"
#include "../compiler.hpp"

namespace mc2lib {
namespace codegen {

/**
 * @namespace mc2lib::codegen::riscv
 * @brief Implementations of Operations for RISC-V (RV64I with atomic extensions)
 *
 * Supports RVWMO (RISC-V Weak Memory Ordering) model with FENCE instructions.
 */
namespace riscv {

// Helper macros for code generation
#define ASM_PRELUDE char *cnext__ = static_cast<char *>(code);
#define ASM_LEN (static_cast<std::size_t>(cnext__ - static_cast<char *>(code)))
#define ASM_AT (start + ASM_LEN)

#define ASM32(v)                                       \
  do {                                                 \
    assert(ASM_LEN + 4 <= len);                        \
    *reinterpret_cast<std::uint32_t *>(cnext__) = (v); \
    cnext__ += 4;                                      \
  } while (0)

#define ASM_PROLOGUE return ASM_LEN;

/**
 * @brief RISC-V RV64I backend for code generation
 * 
 * Generates RISC-V machine code for memory consistency tests.
 * Uses compressed instructions (RVC) where beneficial.
 */
class Backend {
 public:
  void Reset() {}

  // RISC-V supports 1, 2, 4, 8 byte operations
  // For consistency with other backends, start with 1-byte
  static_assert(sizeof(types::WriteID) <= 8, "Unsupported read/write size!");

  // RISC-V register encoding (x0-x31)
  enum Reg {
    zero = 0,  // x0: hardwired zero
    ra = 1,    // x1: return address
    sp = 2,    // x2: stack pointer
    gp = 3,    // x3: global pointer
    tp = 4,    // x4: thread pointer
    t0 = 5,    // x5: temporary
    t1 = 6,    // x6: temporary
    t2 = 7,    // x7: temporary
    s0 = 8,    // x8: saved register / frame pointer
    s1 = 9,    // x9: saved register
    a0 = 10,   // x10: argument / return value
    a1 = 11,   // x11: argument / return value
    a2 = 12,   // x12: argument
    a3 = 13,   // x13: argument
    a4 = 14,   // x14: argument
    a5 = 15,   // x15: argument
    a6 = 16,   // x16: argument
    a7 = 17,   // x17: argument
    s2 = 18,   // x18: saved register
    s3 = 19,   // x19: saved register
    s4 = 20,   // x20: saved register
    s5 = 21,   // x21: saved register
    s6 = 22,   // x22: saved register
    s7 = 23,   // x23: saved register
    s8 = 24,   // x24: saved register
    s9 = 25,   // x25: saved register
    s10 = 26,  // x26: saved register
    s11 = 27,  // x27: saved register
    t3 = 28,   // x28: temporary
    t4 = 29,   // x29: temporary
    t5 = 30,   // x30: temporary
    t6 = 31    // x31: temporary
  };

  /**
   * @brief Generate RET instruction (return from function)
   */
  std::size_t Return(void *code, std::size_t len) const {
    ASM_PRELUDE;
    // ret (pseudo-instruction: jalr x0, 0(x1))
    // Compressed form: c.jr ra
    if (len >= 2) {
      // C.JR ra (compressed: 16-bit)
      *reinterpret_cast<std::uint16_t *>(cnext__) = 0x8082;
      cnext__ += 2;
    } else {
      // jalr x0, x1, 0 (32-bit)
      ASM32(0x00008067);  // jalr x0, 0(ra)
    }
    ASM_PROLOGUE;
  }

  /**
   * @brief Generate NOP instructions for delay
   */
  std::size_t Delay(std::size_t length, void *code, std::size_t len) const {
    ASM_PRELUDE;
    for (std::size_t i = 0; i < length; ++i) {
      // NOP (addi x0, x0, 0)
      // Compressed form: c.nop
      if (ASM_LEN + 2 <= len) {
        *reinterpret_cast<std::uint16_t *>(cnext__) = 0x0001;
        cnext__ += 2;
      } else {
        ASM32(0x00000013);  // addi x0, x0, 0
      }
    }
    ASM_PROLOGUE;
  }

  /**
   * @brief Generate FENCE instruction
   * @param pred Predecessor memory operations (r=read, w=write, rw=both)
   * @param succ Successor memory operations (r=read, w=write, rw=both)
   * 
   * FENCE encoding: 0000_pred_succ_00000_0001111
   * pred/succ bits: [3]=PI, [2]=PO, [1]=PR, [0]=PW
   */
  std::size_t Fence(std::uint8_t pred = 0b1111, std::uint8_t succ = 0b1111,
                    void *code = nullptr, std::size_t len = 4) const {
    ASM_PRELUDE;
    // FENCE pred, succ
    std::uint32_t fence_insn = 0x0000000F;  // Base FENCE opcode
    fence_insn |= (pred & 0xF) << 24;        // Predecessor bits [27:24]
    fence_insn |= (succ & 0xF) << 20;        // Successor bits [23:20]
    ASM32(fence_insn);
    ASM_PROLOGUE;
  }

  /**
   * @brief Generate FENCE.RW (full memory fence)
   */
  std::size_t FenceRW(void *code, std::size_t len) const {
    return Fence(0b11, 0b11, code, len);  // pred=rw, succ=rw
  }

  /**
   * @brief Generate FENCE.W (write fence)
   */
  std::size_t FenceW(void *code, std::size_t len) const {
    return Fence(0b10, 0b10, code, len);  // pred=w, succ=w
  }

  /**
   * @brief Generate FENCE.R (read fence)
   */
  std::size_t FenceR(void *code, std::size_t len) const {
    return Fence(0b01, 0b01, code, len);  // pred=r, succ=r
  }

  /**
   * @brief Load byte and mark event timestamp
   * @param addr Memory address to read from
   * @param out Destination register
   * @param start Instruction pointer start
   * @param code Code buffer
   * @param len Buffer length
   * @param at Output: instruction pointer of load instruction
   */
  std::size_t Read(types::Addr addr, Reg out, types::InstPtr start, void *code,
                   std::size_t len, types::InstPtr *at) const {
    ASM_PRELUDE;

    Helper h(cnext__, code, len);
    
    // Load address into temporary register
    h.LoadImm64(t0, addr);
    
    // LB out, 0(t0) - Load byte
    *at = ASM_AT;
    switch (sizeof(types::WriteID)) {
      case 1:
        ASM32(0x00028003 | (out << 7));  // lb out, 0(t0)
        break;
      case 2:
        ASM32(0x00029003 | (out << 7));  // lh out, 0(t0)
        break;
      case 4:
        ASM32(0x0002A003 | (out << 7));  // lw out, 0(t0)
        break;
      case 8:
        ASM32(0x0002B003 | (out << 7));  // ld out, 0(t0)
        break;
      default:
        throw std::logic_error("Unsupported read size");
    }
    
    ASM_PROLOGUE;
  }

  /**
   * @brief Load byte with address dependency
   * @param addr Memory address
   * @param out Destination register
   * @param dp Dependency register (XOR'd to create dependency chain)
   */
  std::size_t ReadAddrDp(types::Addr addr, Reg out, Reg dp,
                         types::InstPtr start, void *code, std::size_t len,
                         types::InstPtr *at) const {
    ASM_PRELUDE;

    Helper h(cnext__, code, len);
    h.LoadImm64(t0, addr);
    
    // XOR dp, dp, dp - Clear dependency register (creates 0)
    ASM32(0x00000033 | (dp << 7) | (dp << 15) | (dp << 20));  // xor dp, dp, dp
    
    // ADD t0, t0, dp - Add 0 to create address dependency
    ASM32(0x00000033 | (t0 << 7) | (t0 << 15) | (dp << 20));  // add t0, t0, dp
    
    // LB out, 0(t0) - Load byte with dependency
    *at = ASM_AT;
    switch (sizeof(types::WriteID)) {
      case 1:
        ASM32(0x00028003 | (out << 7));  // lb out, 0(t0)
        break;
      case 2:
        ASM32(0x00029003 | (out << 7));  // lh out, 0(t0)
        break;
      case 4:
        ASM32(0x0002A003 | (out << 7));  // lw out, 0(t0)
        break;
      case 8:
        ASM32(0x0002B003 | (out << 7));  // ld out, 0(t0)
        break;
      default:
        throw std::logic_error("Unsupported read size");
    }
    
    ASM_PROLOGUE;
  }

  /**
   * @brief Store byte and mark event timestamp
   * @param addr Memory address to write to
   * @param write_id Value to write
   */
  std::size_t Write(types::Addr addr, types::WriteID write_id,
                    types::InstPtr start, void *code, std::size_t len,
                    types::InstPtr *at) const {
    ASM_PRELUDE;

    Helper h(cnext__, code, len);
    h.LoadImm64(t0, addr);
    
    // LI t1, write_id - Load immediate value
    h.LoadImm64(t1, write_id);
    
    // SB t1, 0(t0) - Store byte
    *at = ASM_AT;
    switch (sizeof(types::WriteID)) {
      case 1:
        ASM32(0x00030023 | (t1 << 20));  // sb t1, 0(t0)
        break;
      case 2:
        ASM32(0x00031023 | (t1 << 20));  // sh t1, 0(t0)
        break;
      case 4:
        ASM32(0x00032023 | (t1 << 20));  // sw t1, 0(t0)
        break;
      case 8:
        ASM32(0x00033023 | (t1 << 20));  // sd t1, 0(t0)
        break;
      default:
        throw std::logic_error("Unsupported write size");
    }
    
    ASM_PROLOGUE;
  }

  /**
   * @brief Atomic Read-Modify-Write operation
   * @param addr Memory address
   * @param write_id Value to write
   * 
   * Uses RISC-V AMO (Atomic Memory Operation) instructions
   */
  std::size_t ReadModifyWrite(types::Addr addr, types::WriteID write_id,
                              types::InstPtr start, void *code, std::size_t len,
                              types::InstPtr *at) const {
    ASM_PRELUDE;

    Helper h(cnext__, code, len);
    h.LoadImm64(t0, addr);
    h.LoadImm64(t1, write_id);
    
    // AMOSWAP.W.aqrl a0, t1, (t0)
    // Format: 0000_1_aq_rl_rs2_rs1_010_rd_0101111
    *at = ASM_AT;
    switch (sizeof(types::WriteID)) {
      case 1:
      case 2:
      case 4:
        // AMOSWAP.W with .aqrl (acquire-release ordering)
        ASM32(0x0802A52F | (a0 << 7) | (t0 << 15) | (t1 << 20));
        break;
      case 8:
        // AMOSWAP.D with .aqrl
        ASM32(0x0802B52F | (a0 << 7) | (t0 << 15) | (t1 << 20));
        break;
      default:
        throw std::logic_error("Unsupported RMW size");
    }
    
    ASM_PROLOGUE;
  }

  /**
   * @brief Cache flush operation (RISC-V doesn't have explicit cache flush in base ISA)
   * Use FENCE.I for instruction cache or FENCE for data
   */
  std::size_t CacheFlush(types::Addr addr, void *code,
                         std::size_t len) const {
    ASM_PRELUDE;
    // FENCE instruction to ensure visibility
    ASM32(0x0000000F | (0b1111 << 24) | (0b1111 << 20));
    ASM_PROLOGUE;
  }

 protected:
  /**
   * @brief Helper class for common code generation patterns
   */
  class Helper {
   public:
    Helper(char *&cnext, void *&code, std::size_t len)
        : cnext__(cnext), code(code), len(len) {}

    /**
     * @brief Load 64-bit immediate into register
     * Uses LUI + ADDI for lower 32 bits, then shifts for upper 32 bits if needed
     */
    void LoadImm64(Reg reg, std::uint64_t imm64) {
      if (imm64 <= 0x7FF) {
        // Small immediate: addi reg, x0, imm
        ASM32(0x00000013 | (reg << 7) | ((imm64 & 0xFFF) << 20));
      } else if (imm64 <= 0xFFFFFFFF) {
        // 32-bit immediate: lui + addi
        std::uint32_t upper = (imm64 + 0x800) >> 12;
        std::uint32_t lower = imm64 & 0xFFF;
        
        // LUI reg, upper
        ASM32(0x00000037 | (reg << 7) | (upper << 12));
        
        // ADDI reg, reg, lower
        if (lower != 0 || (imm64 & 0x800)) {
          ASM32(0x00000013 | (reg << 7) | (reg << 15) | (lower << 20));
        }
      } else {
        // Full 64-bit: Load in multiple steps
        // This is simplified; production code would need more sophisticated approach
        std::uint32_t lo32 = imm64 & 0xFFFFFFFF;
        std::uint32_t hi32 = (imm64 >> 32) & 0xFFFFFFFF;
        
        // Load lower 32 bits
        LoadImm64(reg, lo32);
        
        if (hi32 != 0) {
          // Shift left 32 bits (would need temp register in real implementation)
          // For simplicity, this implementation assumes addresses fit in 32 bits
          throw std::logic_error("64-bit addresses not fully supported yet");
        }
      }
    }

   protected:
    char *&cnext__;
    void *&code;
    std::size_t len;
  };
};

/**
 * @brief Operation types for RISC-V memory consistency tests
 */
enum class OpType {
  Read,
  ReadAddrDp,
  Write,
  ReadModifyWrite,
  Fence,
  FenceRW,
  FenceW,
  FenceR,
  Delay,
  CacheFlush
};

/**
 * @brief Memory operation base class for RISC-V
 */
class MemOperation {
 public:
  virtual ~MemOperation() = default;
  virtual types::Addr addr() const = 0;
  virtual void SetAddr(types::Addr addr) = 0;
  virtual OpType type() const = 0;
};

#undef ASM_PRELUDE
#undef ASM_LEN
#undef ASM_AT
#undef ASM32
#undef ASM_PROLOGUE

}  // namespace riscv
}  // namespace codegen
}  // namespace mc2lib

#endif  // MC2LIB_CODEGEN_OPS_RISCV_HPP_

/* vim: set ts=2 sts=2 sw=2 et : */
