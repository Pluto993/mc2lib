#!/bin/bash
# This file is no longer used.
# The workflow is now manual iteration:
#
#   1. ./host_generate <args> <output_dir>    (first time only)
#   2. Copy thread_*.bin + meta.bin to QEMU guest
#   3. (in QEMU) ./guest_runner <data_dir>
#   4. Copy test_result.bin back to host
#   5. ./host_verify <output_dir>
#   6. Repeat from step 2
echo "This script is deprecated. Use manual iteration workflow."
echo "See Makefile for instructions."
exit 0
# 编译
cd contrib/mcversi/qemu
make all

# 第一次：生成初始测试（ObsLog 模式）
mkdir -p ./data
./host_generate 4 50 8192 16 0x100000000 ./data

# 在 QEMU 中运行（或 QEMU user mode）
qemu-x86_64 -R 0x200000000 ./guest_runner ./data

# 验证 + 生成下一轮
./host_verify ./data

# 重复...
qemu-x86_64 -R 0x200000000 ./guest_runner ./data
./host_verify ./data