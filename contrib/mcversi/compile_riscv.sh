#!/bin/bash
# Compile RISC-V test for gem5

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MC2LIB_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"

echo "🔧 Compiling RISC-V memory consistency test..."
echo "   mc2lib root: $MC2LIB_ROOT"
echo ""

# Compiler settings
CXX=riscv64-linux-gnu-g++
CXXFLAGS="-std=c++11 -g -O2 -static -pthread"
INCLUDES="-I$MC2LIB_ROOT/include -I$SCRIPT_DIR"
DEFINES="-D__riscv=1 -D__riscv_xlen=64"

# gem5 magic instruction library
GEM5_M5_LIB="/root/.openclaw/workspace/gem5/util/m5/build/riscv/out/libm5.a"
GEM5_M5_INCLUDE="/root/.openclaw/workspace/gem5/include"

# Check if gem5 m5 lib exists
if [ ! -f "$GEM5_M5_LIB" ]; then
    echo "⚠️  gem5 m5 library not found at: $GEM5_M5_LIB"
    echo "   Building it now..."
    
    cd /root/.openclaw/workspace/gem5/util/m5
    scons build/riscv/out/m5
    cd -
fi

# Compile
echo "1️⃣ Compiling simple test..."
$CXX $CXXFLAGS $INCLUDES $DEFINES \
    -I$GEM5_M5_INCLUDE \
    -o riscv_simple_test \
    riscv_simple_test.cpp \
    $GEM5_M5_LIB

echo ""
echo "✅ Compilation successful!"
echo "   Output: $SCRIPT_DIR/riscv_simple_test"
echo ""

# Show file info
file riscv_simple_test
ls -lh riscv_simple_test

echo ""
echo "🚀 Ready to run in gem5!"
echo ""
echo "Next steps:"
echo "  1. Copy to gem5 tests: cp riscv_simple_test /root/.openclaw/workspace/gem5/tests/mc2lib_test"
echo "  2. Create gem5 config script"
echo "  3. Run: gem5.opt <config.py> --binary=tests/mc2lib_test"
