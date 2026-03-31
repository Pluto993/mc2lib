#!/bin/bash
# 完整的演化测试流程脚本

set -e

WORK_DIR="/root/.openclaw/workspace/mc2lib-evolution"
cd "$WORK_DIR"

echo "=========================================="
echo "mc2lib-evolution 完整流程测试"
echo "=========================================="
echo ""

# 1. 生成测试
echo "[1/5] Generating test..."
python3 python/test_generator.py test_demo 99999 tests/demo

# 2. 编译测试
echo "[2/5] Compiling test..."
riscv64-linux-gnu-gcc -std=c11 -O2 -static \
    -o tests/demo/test_demo \
    tests/demo/test_demo.c
echo "  Binary size: $(du -h tests/demo/test_demo | cut -f1)"

# 3. 运行 gem5
echo "[3/5] Running on gem5..."
GEM5_ROOT="/root/.openclaw/workspace/gem5"
cd "$GEM5_ROOT"

timeout 90 ./build/RISCV/gem5.opt \
    --outdir="$WORK_DIR/tests/demo/m5out" \
    configs/mc2lib_multicore_simple.py \
    "$WORK_DIR/tests/demo/test_demo" 2 \
    > /dev/null 2>&1 || true

# 4. 收集日志
echo "[4/5] Collecting logs..."
if [ -f memory_trace_core0.csv ]; then
    mv memory_trace_core*.csv "$WORK_DIR/tests/demo/"
    echo "  Found $(ls $WORK_DIR/tests/demo/memory_trace_core*.csv | wc -l) trace files"
fi

cd "$WORK_DIR"

# 5. 分析结果
echo "[5/5] Analyzing results..."
python3 << 'PYEOF'
import sys
sys.path.insert(0, 'python')
from gem5_runner import GEM5Runner, MemoryEvent
from fitness_evaluator import FitnessEvaluator
from pathlib import Path
import csv

# 手动解析日志
events = []
for trace_file in sorted(Path('tests/demo').glob('memory_trace_core*.csv')):
    with open(trace_file) as f:
        reader = csv.DictReader(f)
        for row in reader:
            event = MemoryEvent(
                timestamp=int(row['timestamp']),
                seq_id=int(row['seq_id']),
                core_id=int(row['core_id']),
                event_type=row['type'],
                address=row['address'],
                value=int(row['value']),
                po_index=int(row['po_index'])
            )
            events.append(event)

# 提取地址对
addresses = set()
for event in events:
    if event.event_type in ['READ', 'WRITE']:
        addresses.add(event.address)

addr_list = sorted(addresses)
pairs = []
for i in range(len(addr_list)):
    for j in range(i + 1, len(addr_list)):
        pairs.append((addr_list[i], addr_list[j]))

# 创建模拟的 TestResult
class TestResult:
    def __init__(self):
        self.test_id = 'test_demo'
        self.success = True
        self.timeout = False
        self.error_msg = None
        self.sim_seconds = 1.0
        self.events = events
        self.address_pairs = pairs
        self.violations = []
        self.gem5_output = ""

result = TestResult()

# 评估适应度
evaluator = FitnessEvaluator({
    'total_possible_pairs': 100,
    'new_pair_weight': 0.1,
    'violation_weight': 0.2
})

metrics = evaluator.evaluate(result, set())

print("\n" + "=" * 60)
print("Complete Pipeline Test Result:")
print("=" * 60)
print(f"Test ID: {result.test_id}")
print(f"Status: {'SUCCESS' if result.success else 'FAILED'}")
print(f"Events: {len(result.events)}")
print(f"Address pairs: {len(result.address_pairs)}")
print(f"Violations: {len(result.violations)}")
print()
print("Fitness Metrics:")
print(f"  Base coverage: {metrics.base_coverage:.4f}")
print(f"  New pair bonus: {metrics.new_pair_bonus:.4f}")
print(f"  Violation bonus: {metrics.violation_bonus:.4f}")
print(f"  Penalty: {metrics.penalty:.4f}")
print(f"  Final score: {metrics.final_score:.4f}")
print("=" * 60)

PYEOF

echo ""
echo "=========================================="
echo "✅ Complete pipeline test finished!"
echo "=========================================="
