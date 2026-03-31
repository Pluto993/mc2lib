#!/bin/bash
# Quick demo: Run memory consistency test and check results

set -e

echo "========================================="
echo "RISC-V Memory Consistency Test Demo"
echo "========================================="
echo ""

cd /root/.openclaw/workspace/mc2lib/contrib/mcversi

# Step 1: Compile (if not already compiled)
if [ ! -f traced_test_x86 ]; then
    echo "📦 Compiling test..."
    g++ -std=c++11 -g -O2 -pthread -I../../include \
        -o traced_test_x86 \
        riscv_traced_test.cpp
    echo "✅ Compilation done"
    echo ""
fi

# Step 2: Run test
echo "🧪 Running test (2 cores, 100 iterations)..."
./traced_test_x86 2

echo ""
echo "========================================="
echo ""

# Step 3: Analyze trace
echo "📊 Analyzing memory trace..."
echo ""
python3 consistency_checker.py memory_trace.csv

echo ""
echo "========================================="
echo "Demo complete!"
echo "========================================="
echo ""
echo "Files generated:"
echo "  - memory_trace.csv (event log)"
echo ""
echo "Try different tests by editing riscv_traced_test.cpp:"
echo "  - test_store_buffering()  (current)"
echo "  - test_message_passing()"
echo "  - test_load_buffering()"
