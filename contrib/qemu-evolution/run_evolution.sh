#!/bin/bash
# QEMU 遗传算法演化 - 一键运行脚本

set -e

echo "========================================"
echo "QEMU 遗传算法演化框架"
echo "========================================"
echo ""

# 检查依赖
echo "检查依赖..."
if ! command -v qemu-riscv64 &> /dev/null; then
    echo "❌ qemu-riscv64 未安装"
    echo "请运行: yum install -y qemu-user"
    exit 1
fi

if ! command -v riscv64-linux-gnu-gcc &> /dev/null; then
    echo "❌ riscv64-linux-gnu-gcc 未安装"
    echo "请运行: yum install -y gcc-riscv64-linux-gnu"
    exit 1
fi

echo "✅ 依赖检查通过"
echo ""

# 第一代测试
echo "========================================"
echo "第一代测试（基准）"
echo "========================================"

if [ ! -f simple_mc2lib_test ]; then
    echo "编译第一代测试..."
    riscv64-linux-gnu-gcc -static -O2 -o simple_mc2lib_test simple_mc2lib_test.c
    echo "✅ 编译成功"
fi

echo "运行第一代测试..."
qemu-riscv64 ./simple_mc2lib_test > mc2lib_qemu_log.txt 2>&1
EVENT_COUNT=$(grep -c '^TRACE:' mc2lib_qemu_log.txt)
echo "✅ 第一代完成：$EVENT_COUNT 个事件"
echo ""

# 生成第二代
echo "========================================"
echo "生成第二代测试"
echo "========================================"
echo "运行遗传算法..."
python3 evolve_gen2.py
echo ""

# 编译第二代
echo "========================================"
echo "编译第二代测试"
echo "========================================"
for test in gen2_test_*.c; do
    if [ -f "$test" ]; then
        name=${test%.c}
        echo "编译 $name..."
        riscv64-linux-gnu-gcc -static -O2 -o $name $test
    fi
done
echo "✅ 所有第二代测试编译完成"
echo ""

# 运行第二代
echo "========================================"
echo "运行第二代测试"
echo "========================================"
for test in gen2_test_0 gen2_test_1 gen2_test_2; do
    if [ -f "$test" ]; then
        echo "运行 $test..."
        qemu-riscv64 ./$test > ${test}_log.txt 2>&1
        EVENT_COUNT=$(grep -c '^TRACE:' ${test}_log.txt)
        echo "  ✓ $EVENT_COUNT 个事件"
    fi
done
echo ""

# 分析结果
echo "========================================"
echo "分析演化结果"
echo "========================================"
python3 analyze_evolution.py
echo ""

echo "========================================"
echo "✅ 演化完成！"
echo "========================================"
echo ""
echo "📁 输出文件:"
echo "  - mc2lib_qemu_log.txt       (第一代日志)"
echo "  - gen2_test_*_log.txt       (第二代日志)"
echo "  - gen2_test_*.c             (第二代源码)"
echo ""
echo "📊 查看详细结果:"
echo "  - cat mc2lib_qemu_log.txt"
echo "  - python3 analyze_evolution.py"
