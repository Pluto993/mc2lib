// Test file for RISC-V backend
// Verifies code generation for RISC-V memory operations

#include "mc2lib/codegen/ops/riscv.hpp"
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

using namespace mc2lib;
using namespace mc2lib::codegen::riscv;

// Test basic backend instantiation
TEST(RISCV_Backend, Instantiation) {
  Backend backend;
  backend.Reset();
  SUCCEED();
}

// Test Return instruction generation
TEST(RISCV_Backend, Return) {
  Backend backend;
  std::vector<char> code(16, 0);
  
  std::size_t len = backend.Return(code.data(), code.size());
  
  EXPECT_GE(len, 2);  // At least 2 bytes for compressed RET
  EXPECT_LE(len, 4);  // At most 4 bytes
  
  // Check for compressed C.JR ra (0x8082) or JALR x0, 0(ra)
  if (len == 2) {
    std::uint16_t insn = *reinterpret_cast<std::uint16_t*>(code.data());
    EXPECT_EQ(insn, 0x8082);  // C.JR ra
  }
}

// Test NOP/Delay generation
TEST(RISCV_Backend, Delay) {
  Backend backend;
  std::vector<char> code(64, 0);
  
  const std::size_t delay_count = 10;
  std::size_t len = backend.Delay(delay_count, code.data(), code.size());
  
  EXPECT_GE(len, delay_count * 2);  // At least 2 bytes per NOP (compressed)
  EXPECT_LE(len, delay_count * 4);  // At most 4 bytes per NOP
}

// Test FENCE instruction generation
TEST(RISCV_Backend, Fence) {
  Backend backend;
  std::vector<char> code(16, 0);
  
  std::size_t len = backend.Fence(0b11, 0b11, code.data(), code.size());
  
  EXPECT_EQ(len, 4);  // FENCE is always 32-bit
  
  std::uint32_t insn = *reinterpret_cast<std::uint32_t*>(code.data());
  
  // Check FENCE opcode (bits [6:0] = 0001111)
  EXPECT_EQ(insn & 0x7F, 0x0F);
  
  // Check pred bits [27:24] = 0b0011 (rw)
  EXPECT_EQ((insn >> 24) & 0xF, 0b11);
  
  // Check succ bits [23:20] = 0b0011 (rw)
  EXPECT_EQ((insn >> 20) & 0xF, 0b11);
}

// Test FENCE.RW (full memory fence)
TEST(RISCV_Backend, FenceRW) {
  Backend backend;
  std::vector<char> code(16, 0);
  
  std::size_t len = backend.FenceRW(code.data(), code.size());
  
  EXPECT_EQ(len, 4);
  
  std::uint32_t insn = *reinterpret_cast<std::uint32_t*>(code.data());
  EXPECT_EQ(insn & 0x7F, 0x0F);  // FENCE opcode
}

// Test Read (load byte) instruction
TEST(RISCV_Backend, Read) {
  Backend backend;
  std::vector<char> code(64, 0);
  
  const types::Addr test_addr = 0x1000;
  types::InstPtr at = 0;
  
  std::size_t len = backend.Read(test_addr, Backend::Reg::a0, 0, 
                                  code.data(), code.size(), &at);
  
  EXPECT_GT(len, 0);
  EXPECT_GT(at, 0);  // Should point to the actual load instruction
}

// Test Write (store byte) instruction
TEST(RISCV_Backend, Write) {
  Backend backend;
  std::vector<char> code(64, 0);
  
  const types::Addr test_addr = 0x2000;
  const types::WriteID write_val = 42;
  types::InstPtr at = 0;
  
  std::size_t len = backend.Write(test_addr, write_val, 0,
                                   code.data(), code.size(), &at);
  
  EXPECT_GT(len, 0);
  EXPECT_GT(at, 0);  // Should point to the actual store instruction
}

// Test ReadAddrDp (load with address dependency)
TEST(RISCV_Backend, ReadAddrDp) {
  Backend backend;
  std::vector<char> code(64, 0);
  
  const types::Addr test_addr = 0x3000;
  types::InstPtr at = 0;
  
  std::size_t len = backend.ReadAddrDp(test_addr, Backend::Reg::a1, 
                                       Backend::Reg::t2, 0,
                                       code.data(), code.size(), &at);
  
  EXPECT_GT(len, 0);
  EXPECT_GT(at, 0);
}

// Test ReadModifyWrite (atomic operation)
TEST(RISCV_Backend, ReadModifyWrite) {
  Backend backend;
  std::vector<char> code(64, 0);
  
  const types::Addr test_addr = 0x4000;
  const types::WriteID write_val = 99;
  types::InstPtr at = 0;
  
  std::size_t len = backend.ReadModifyWrite(test_addr, write_val, 0,
                                             code.data(), code.size(), &at);
  
  EXPECT_GT(len, 0);
  EXPECT_GT(at, 0);
  
  // The instruction at 'at' should be an AMOSWAP instruction
  std::uint32_t* insn_ptr = reinterpret_cast<std::uint32_t*>(
      code.data() + at);
  std::uint32_t insn = *insn_ptr;
  
  // Check AMOSWAP opcode (bits [6:0] = 0101111, funct3 = 010/011)
  EXPECT_EQ(insn & 0x7F, 0x2F);
}

// Test CacheFlush
TEST(RISCV_Backend, CacheFlush) {
  Backend backend;
  std::vector<char> code(16, 0);
  
  std::size_t len = backend.CacheFlush(0x5000, code.data(), code.size());
  
  EXPECT_GT(len, 0);
  
  // Should generate a FENCE instruction
  std::uint32_t insn = *reinterpret_cast<std::uint32_t*>(code.data());
  EXPECT_EQ(insn & 0x7F, 0x0F);  // FENCE opcode
}

// Test multiple operations in sequence
TEST(RISCV_Backend, SequenceOfOperations) {
  Backend backend;
  std::vector<char> code(256, 0);
  char* code_ptr = code.data();
  std::size_t remaining = code.size();
  std::size_t total_len = 0;
  
  // 1. Fence
  std::size_t len1 = backend.FenceRW(code_ptr, remaining);
  EXPECT_GT(len1, 0);
  code_ptr += len1;
  remaining -= len1;
  total_len += len1;
  
  // 2. Write
  types::InstPtr at_write = 0;
  std::size_t len2 = backend.Write(0x1000, 1, total_len, 
                                    code_ptr, remaining, &at_write);
  EXPECT_GT(len2, 0);
  code_ptr += len2;
  remaining -= len2;
  total_len += len2;
  
  // 3. Fence
  std::size_t len3 = backend.FenceRW(code_ptr, remaining);
  EXPECT_GT(len3, 0);
  code_ptr += len3;
  remaining -= len3;
  total_len += len3;
  
  // 4. Read
  types::InstPtr at_read = 0;
  std::size_t len4 = backend.Read(0x2000, Backend::Reg::a0, total_len,
                                   code_ptr, remaining, &at_read);
  EXPECT_GT(len4, 0);
  code_ptr += len4;
  remaining -= len4;
  total_len += len4;
  
  // 5. Return
  std::size_t len5 = backend.Return(code_ptr, remaining);
  EXPECT_GT(len5, 0);
  total_len += len5;
  
  EXPECT_LT(total_len, code.size());
}

// Test small immediate loads (indirectly through Read which uses LoadImm64)
TEST(RISCV_Backend, SmallAddress) {
  Backend backend;
  std::vector<char> code(64, 0);
  types::InstPtr at = 0;
  
  // Small address should not throw
  EXPECT_NO_THROW(
    backend.Read(0x123, Backend::Reg::a0, 0, code.data(), code.size(), &at)
  );
}

// Test medium immediate loads
TEST(RISCV_Backend, MediumAddress) {
  Backend backend;
  std::vector<char> code(64, 0);
  types::InstPtr at = 0;
  
  // 32-bit address should not throw
  EXPECT_NO_THROW(
    backend.Read(0x12345678, Backend::Reg::a0, 0, code.data(), code.size(), &at)
  );
}

// Test large immediate loads
TEST(RISCV_Backend, LargeAddress) {
  Backend backend;
  std::vector<char> code(64, 0);
  types::InstPtr at = 0;
  
  // 64-bit address currently throws
  EXPECT_THROW(
    backend.Read(0x1234567890ABCDEF, Backend::Reg::a0, 0, 
                 code.data(), code.size(), &at),
    std::logic_error
  );
}

