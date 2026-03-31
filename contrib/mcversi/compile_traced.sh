#!/bin/bash
# Compile RISC-V traced test

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MC2LIB_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "🔧 Compiling RISC-V Memory Consistency Tracer Test..."
echo ""

# Compiler
CXX=riscv64-linux-gnu-g++
CXXFLAGS="-std=c++11 -g -O2 -pthread"
INCLUDES="-I$MC2LIB_ROOT/include"

# Compile
echo "Compiling..."
$CXX $CXXFLAGS $INCLUDES \
    -o riscv_traced_test \
    riscv_traced_test.cpp

echo ""
echo "✅ Compilation successful!"
echo "   Output: $SCRIPT_DIR/riscv_traced_test"
echo ""

file riscv_traced_test
ls -lh riscv_traced_test

echo ""
echo "🚀 Ready to run!"
echo ""
echo "Run in gem5:"
echo "  1. Copy binary to gem5:"
echo "     cp riscv_traced_test /root/.openclaw/workspace/gem5/tests/"
echo ""
echo "  2. Run gem5 simulation:"
echo "     cd /root/.openclaw/workspace/gem5"
echo "     ./build/RISCV/gem5.opt configs/example/se.py \\"
echo "       --cmd=tests/riscv_traced_test \\"
echo "       --cpu-type=TimingSimpleCPU \\"
echo "       --num-cpus=2"
echo ""
echo "  3. Analyze trace:"
echo "     python3 consistency_checker.py memory_trace.csv"
