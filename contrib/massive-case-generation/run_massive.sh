#!/bin/bash
# 一键运行脚本：生成、编译、运行超大规模测试

set -e

SCALE="${1:-medium}"
PATTERN="${2:-mixed}"

echo "========================================"
echo "超大规模内存一致性测试 - 完整流程"
echo "========================================"
echo "规模: $SCALE"
echo "模式: $PATTERN"
echo "========================================"
echo ""

# 1. 生成测试
echo "步骤 1/3: 生成测试..."
python3 generate_massive_tests.py --scale "$SCALE" --pattern "$PATTERN"
echo ""

# 2. 编译测试
echo "步骤 2/3: 编译测试..."
compiled=0
for test in massive_test_${SCALE}_${PATTERN}_*.c; do
    [ -f "$test" ] || continue
    name="${test%.c}"
    echo "  编译: $name"
    riscv64-linux-gnu-gcc -static -O0 -o "$name" "$test"
    ((compiled++))
done
echo "  完成: $compiled 个测试"
echo ""

# 3. 运行测试
echo "步骤 3/3: 运行测试..."
run=0
for test in massive_test_${SCALE}_${PATTERN}_*; do
    [ -x "$test" ] || continue
    echo "  运行: $test"
    timeout 600 qemu-riscv64 "./$test" > "${test}_log.txt" || echo "    (超时或失败)"
    ((run++))
done
echo "  完成: $run 个测试"
echo ""

# 4. 统计
echo "========================================"
echo "完成统计"
echo "========================================"
echo "生成的 .c 文件: $(ls massive_test_${SCALE}_${PATTERN}_*.c 2>/dev/null | wc -l)"
echo "编译的二进制: $(ls massive_test_${SCALE}_${PATTERN}_* 2>/dev/null | grep -v '\\.c$' | grep -v '_log\\.txt$' | wc -l)"
echo "生成的日志: $(ls massive_test_${SCALE}_${PATTERN}_*_log.txt 2>/dev/null | wc -l)"
echo ""

# 5. 快速分析
echo "快速分析 (前 3 个日志):"
for log in massive_test_${SCALE}_${PATTERN}_*_log.txt; do
    [ -f "$log" ] || continue
    events=$(grep -c "^TRACE:" "$log" || true)
    echo "  $log: $events 事件"
    [ $(ls massive_test_${SCALE}_${PATTERN}_*_log.txt 2>/dev/null | wc -l) -gt 3 ] && break
done

echo ""
echo "========================================"
echo "✅ 完整流程完成！"
echo "========================================"
echo ""
echo "分析结果:"
echo "  python3 analyze_massive_results.py"
