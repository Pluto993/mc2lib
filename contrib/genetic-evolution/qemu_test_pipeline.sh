#!/bin/bash
# QEMU TSO/RVWMO 测试自动化流程

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "🚀 QEMU TSO/RVWMO 测试自动化流程"
echo "=================================="

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# 配置
TSO_SOURCE="store_buffering_tso.c"
RVWMO_SOURCE="store_buffering_rvwmo.c"
ITERATIONS=100

# 步骤1: 编译 TSO 版本
echo -e "\n${YELLOW}[1/5]${NC} 编译 TSO 版本..."
riscv64-linux-gnu-gcc -march=rv64gc_ztso -DRISCV_TSO -static -o test_tso "$TSO_SOURCE"
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ TSO 编译成功${NC}"
else
    echo -e "${RED}❌ TSO 编译失败${NC}"
    exit 1
fi

# 步骤2: 编译 RVWMO 版本
echo -e "\n${YELLOW}[2/5]${NC} 编译 RVWMO 版本..."
riscv64-linux-gnu-gcc -march=rv64gc -static -o test_rvwmo "$RVWMO_SOURCE"
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ RVWMO 编译成功${NC}"
else
    echo -e "${RED}❌ RVWMO 编译失败${NC}"
    exit 1
fi

# 步骤3: 运行 TSO 测试
echo -e "\n${YELLOW}[3/5]${NC} 运行 TSO 测试 (QEMU 用户态)..."
qemu-riscv64 test_tso > tso_trace.txt 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ TSO 测试完成${NC}"
    echo "   输出: tso_trace.txt ($(wc -l < tso_trace.txt) 行)"
else
    echo -e "${RED}❌ TSO 测试失败${NC}"
    exit 1
fi

# 步骤4: 运行 RVWMO 测试
echo -e "\n${YELLOW}[4/5]${NC} 运行 RVWMO 测试 (QEMU 用户态)..."
qemu-riscv64 test_rvwmo > rvwmo_trace.txt 2>&1
if [ $? -eq 0 ]; then
    echo -e "${GREEN}✅ RVWMO 测试完成${NC}"
    echo "   输出: rvwmo_trace.txt ($(wc -l < rvwmo_trace.txt) 行)"
else
    echo -e "${RED}❌ RVWMO 测试失败${NC}"
    exit 1
fi

# 步骤5: 验证和对比
echo -e "\n${YELLOW}[5/5]${NC} 验证结果并对比差异..."
python3 qemu_tso_verifier.py tso_trace.txt rvwmo_trace.txt

# 结果判定
echo -e "\n=================================="
if [ -f comparison_result.json ]; then
    TSO_VIOLATIONS=$(jq -r '.tso.violation_count' comparison_result.json)
    RVWMO_VIOLATIONS=$(jq -r '.rvwmo.violation_count' comparison_result.json)
    
    echo -e "${GREEN}📊 测试结果汇总:${NC}"
    echo "  TSO 违例:   $TSO_VIOLATIONS"
    echo "  RVWMO 违例: $RVWMO_VIOLATIONS"
    
    if [ "$TSO_VIOLATIONS" -eq 0 ]; then
        echo -e "${GREEN}✅ TSO 模型正确 - 无违例${NC}"
    else
        echo -e "${RED}⚠️  TSO 模型可能有问题 - 发现违例${NC}"
    fi
    
    if [ "$RVWMO_VIOLATIONS" -gt "$TSO_VIOLATIONS" ]; then
        echo -e "${GREEN}✅ RVWMO 允许更多重排序 - 符合预期${NC}"
    fi
else
    echo -e "${RED}❌ 无法读取结果文件${NC}"
fi

echo -e "\n${GREEN}🎉 流程完成！${NC}"
echo "生成的文件:"
echo "  - test_tso, test_rvwmo (可执行文件)"
echo "  - tso_trace.txt, rvwmo_trace.txt (原始 trace)"
echo "  - comparison_result.json (对比结果)"
