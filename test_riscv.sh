#!/bin/bash
# 一键编译和测试 mc2lib RISC-V backend

set -e  # 遇到错误立即退出

echo "🔧 开始编译和测试 mc2lib RISC-V backend..."
echo ""

# 进入项目目录
cd /root/.openclaw/workspace/mc2lib

# 1. 清理
echo "1️⃣ 清理旧文件..."
make clean
echo ""

# 2. 编译
echo "2️⃣ 编译测试程序..."
make test_mc2lib
echo ""

# 3. 运行 RISC-V 测试
echo "3️⃣ 运行 RISC-V 测试（14 个）..."
./test_mc2lib --gtest_filter="RISCV*"
echo ""

# 4. 运行所有测试
echo "4️⃣ 运行所有测试（41 个）..."
./test_mc2lib
echo ""

echo "✅ 所有测试通过！"
echo ""
echo "📊 统计信息："
echo "   - RISC-V 测试: 14 个"
echo "   - 总测试数: 41 个"
echo "   - 通过率: 100%"
echo ""
echo "📁 生成的文件："
echo "   - include/mc2lib/codegen/ops/riscv.hpp"
echo "   - src/test_codegen_riscv.cpp"
echo "   - test_mc2lib (可执行文件)"
echo ""
echo "🚀 下一步可以："
echo "   1. 在 gem5 中运行测试"
echo "   2. 创建 RISC-V RandomFactory"
echo "   3. 实现 RVWMO 检查器"
