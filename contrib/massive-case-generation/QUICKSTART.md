# 快速入门指南

## 🚀 5 分钟快速开始

### 1. 生成小规模测试

```bash
cd /root/.openclaw/workspace/mc2lib/contrib/massive-case-generation

# 生成 5 个小规模测试（1000 次迭代）
python3 generate_massive_tests.py --scale small --pattern mixed
```

### 2. 编译测试

```bash
# 编译所有生成的测试
for test in massive_test_small_mixed_*.c; do
    name=${test%.c}
    echo "编译: $name"
    riscv64-linux-gnu-gcc -static -O2 -o $name $test
done
```

### 3. 运行测试

```bash
# 运行所有测试（每个约 2-5 秒）
for test in massive_test_small_mixed_*; do
    [ -x "$test" ] && echo "运行: $test" && qemu-riscv64 ./$test > ${test}_log.txt
done
```

### 4. 分析结果

```bash
python3 analyze_massive_results.py
```

---

## 📊 示例输出

### 生成阶段

```
======================================================================
超大规模内存一致性测试生成器
======================================================================
规模模式: small
测试模式: mixed
迭代次数: 1,000
地址数量: 100
最大事件: 10,000
模式长度: 50
测试数量: 5
======================================================================

生成 5 个测试...
  [1/5] massive_test_small_mixed_0...
      ✓ ./massive_test_small_mixed_0.c (34 operations)
  ...
```

### 运行阶段

```
========================================
RISC-V Massive Memory Consistency Test
========================================
Test ID: massive_test_small_mixed_0
Scale: small
Iterations: 1000
Max events: 10000
Pattern length: 34
Address space: 100 addresses
========================================

Running test...
  Progress: 0% (0 / 1000)
  Progress: 10% (100 / 1000)
  ...
  Progress: 100% (1000 / 1000)
  
Test iterations complete
Elapsed time: 2 seconds

=== Memory Trace ===
TRACE:...,0,0,WRITE,0x...,1,0
TRACE:...,1,0,READ,0x...,0,1
...
=== End Trace (34000 events) ===
```

### 分析阶段

```
================================================================================
超大规模测试结果分析
================================================================================

总测试数: 5

汇总统计:
  总事件数: 170,000
  总地址数: 150
  总地址对: 500
  平均事件/测试: 34,000

================================================================================
详细结果
================================================================================

文件                                               事件      地址    地址对
--------------------------------------------------------------------------------
massive_test_small_mixed_0_log.txt               34,000       30        45
massive_test_small_mixed_1_log.txt               34,000       30        45
...

================================================================================
最佳结果
================================================================================

最多事件: massive_test_small_mixed_0_log.txt
  事件数: 34,000
  类型分布: {'WRITE': 15000, 'READ': 15000, 'FENCE': 4000}
...
```

---

## 🎯 不同规模的典型用例

### Small（小型）- 快速验证

```bash
python3 generate_massive_tests.py --scale small --pattern random
# 用途: 快速测试生成器和工具链
# 时间: 生成 1s + 编译 5s + 运行 10s = ~16s
```

### Medium（中型）- 常规测试

```bash
python3 generate_massive_tests.py --scale medium --pattern mixed
# 用途: 日常测试和开发
# 时间: 生成 2s + 编译 10s + 运行 100s = ~2min
```

### Large（大型）- 深度探索

```bash
python3 generate_massive_tests.py --scale large --pattern sb
# 用途: 深入探索特定内存模式
# 时间: 生成 5s + 编译 15s + 运行 600s = ~10min
```

### Massive（超大）- 压力测试

```bash
python3 generate_massive_tests.py --scale massive --pattern mixed
# 用途: 发现罕见竞态条件
# 时间: 生成 10s + 编译 30s + 运行 3000s = ~50min
```

### Extreme（极限）- 极限探索

```bash
python3 generate_massive_tests.py --scale extreme --pattern random
# 用途: 极限压力测试
# 时间: 生成 30s + 编译 60s + 运行 15000s = ~4h
# ⚠️ 需要 > 4GB RAM
```

---

## 🔧 常见问题

### Q: 如何只生成一个测试？

修改配置或手动编辑 `generate_massive_tests.py` 中的 `num_tests`。

### Q: 如何自定义地址数量？

```python
# 在 generate_massive_tests.py 中修改 CONFIGS
CONFIGS[ScaleMode.CUSTOM] = GenerationConfig(
    mode=ScaleMode.CUSTOM,
    num_iterations=5000,
    num_addresses=200,  # 自定义
    max_events=50000,
    pattern_length=100,
    num_tests=3,
    mutation_rate=0.25
)
```

### Q: 测试运行太慢怎么办？

- 减少迭代次数
- 减少模式长度
- 使用更小的规模
- 使用 `timeout` 命令限制时间

### Q: 内存不足怎么办？

- 减少 `max_events`
- 使用更小的规模
- 编译时添加优化: `-O3 -s`

### Q: 如何在 gem5 中运行？

```bash
# 1. 编译 RISC-V 版本
riscv64-linux-gnu-gcc -static -O2 -o test massive_test_*.c

# 2. 运行 gem5
/path/to/gem5/build/RISCV/gem5.opt \
    configs/example/se.py \
    --cmd=test \
    --cpu-type=TimingSimpleCPU \
    --num-cpus=1

# 注意: gem5 会非常慢（预计 10x - 100x 时间）
```

---

## 📚 进阶用法

### 1. 批量生成多种模式

```bash
for pattern in sb mp lb random loop rmw mixed; do
    python3 generate_massive_tests.py --scale medium --pattern $pattern
done
```

### 2. 使用一键脚本

```bash
# 生成 + 编译 + 运行 + 分析
./run_massive.sh medium mixed
```

### 3. 自定义分析

```python
# analyze_massive_results.py 支持自定义
results = analyze_all_logs("my_custom_*_log.txt")
```

---

## ✅ 验证安装

```bash
# 检查依赖
which python3         # Python 3.7+
which riscv64-linux-gnu-gcc  # RISC-V 编译器
which qemu-riscv64    # QEMU 用户模式

# 测试生成器
python3 generate_massive_tests.py --scale small --pattern random

# 测试编译
ls massive_test_*.c > /dev/null && echo "生成成功"

# 测试运行（如果有 QEMU）
qemu-riscv64 --version > /dev/null && echo "QEMU 可用"
```

---

## 🎉 下一步

1. **阅读完整文档**: [README.md](README.md)
2. **学习设计思想**: [LEARNING_NOTES.md](LEARNING_NOTES.md)
3. **尝试不同规模**: 从 small → extreme
4. **探索不同模式**: 7 种测试模式
5. **贡献改进**: 欢迎 PR 和建议

---

**Happy Testing! 🚀**
