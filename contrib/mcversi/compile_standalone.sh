#!/bin/bash
# Compile standalone RISC-V test (no gem5 dependencies)

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MC2LIB_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "🔧 Compiling standalone RISC-V memory consistency test..."
echo ""

# Compiler settings
CXX=riscv64-linux-gnu-g++
CXXFLAGS="-std=c++11 -g -O2 -pthread"
INCLUDES="-I$MC2LIB_ROOT/include"

# Compile
echo "Compiling..."
$CXX $CXXFLAGS $INCLUDES \
    -o riscv_standalone_test \
    riscv_standalone_test.cpp

echo ""
echo "✅ Compilation successful!"
echo "   Output: $SCRIPT_DIR/riscv_standalone_test"
echo ""

# Show file info
file riscv_standalone_test
ls -lh riscv_standalone_test

echo ""
echo "🚀 Ready to run!"
echo ""
echo "Run locally (if RISC-V CPU available):"
echo "  ./riscv_standalone_test"
echo ""
echo "Run in gem5:"
echo "  1. Copy binary: cp riscv_standalone_test /root/.openclaw/workspace/gem5/tests/"
echo "  2. Run: cd /root/.openclaw/workspace/gem5"
echo "  3. ./build/RISCV/gem5.opt <config.py> --cmd=tests/riscv_standalone_test"
