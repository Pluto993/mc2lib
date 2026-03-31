# RISC-V TSO Memory Model Support

## 📚 概述

本扩展为 mc2lib 添加了 **RISC-V TSO (Total Store Order)** 内存模型支持，基于 RISC-V Ztso 扩展规范。

### 支持的内存模型

1. **RVWMO** (RISC-V Weak Memory Ordering) - 默认模型
   - 允许更激进的重排序
   - 需要显式 fence 指令保证顺序

2. **TSO** (Total Store Order) - Ztso 扩展
   - 类似 x86-TSO 的强内存模型
   - 使用 `fence.tso` 指令
   - 禁止 Store Buffering 行为

---

## 🎯 TSO vs RVWMO 关键差异

### Store Buffering 测试

```c
// Thread 0        | Thread 1
x = 1;             | y = 1;
fence;             | fence;
r0 = y;            | r1 = x;
```

**允许的结果**:
- RVWMO: ✅ `r0 = 0 && r1 = 0` (允许)
- TSO:   ❌ `r0 = 0 && r1 = 0` (禁止)

### Fence 指令差异

| 模型 | Fence 指令 | 语义 |
|------|-----------|------|
| RVWMO | `fence rw,rw` | 显式全屏障 |
| TSO | `fence.tso` | 仅 Store-Load 屏障 |

---

## 🚀 使用方法

### 1. 生成测试

```bash
cd contrib/genetic-evolution
python3 python/riscv_tso_model.py
```

生成的测试文件：
- `store_buffering_tso.c` - TSO 模型测试
- `store_buffering_rvwmo.c` - RVWMO 模型测试
- `message_passing_tso.c` - 消息传递测试
- `load_buffering_tso.c` - 加载缓冲测试

### 2. 编译测试

#### TSO 模型（使用 fence.tso）

```bash
gcc -DRISCV_TSO -pthread -o store_buffering_tso store_buffering_tso.c
```

#### RVWMO 模型（使用标准 fence）

```bash
gcc -pthread -o store_buffering_rvwmo store_buffering_rvwmo.c
```

### 3. 运行测试

```bash
# 运行 TSO 测试
./store_buffering_tso

# 运行 RVWMO 测试
./store_buffering_rvwmo
```

---

## 📊 测试类型

### 1. Store Buffering (SB)

检测 store buffer 重排序。

**模式**:
```
Thread 0: x = 1; fence; r0 = y
Thread 1: y = 1; fence; r1 = x
```

**关键结果**: `r0 = 0 && r1 = 0`
- RVWMO: 可能出现
- TSO: 不应出现

### 2. Message Passing (MP)

检测因果关系违例。

**模式**:
```
Thread 0: x = 1; fence.w,w; y = 1
Thread 1: r0 = y; fence.r,r; r1 = x
```

**关键结果**: 如果 `r0 = 1` 则 `r1 = 1`
- 两种模型都应保证

### 3. Load Buffering (LB)

检测循环依赖。

**模式**:
```
Thread 0: r0 = y; fence; x = 1
Thread 1: r1 = x; fence; y = 1
```

**关键结果**: `r0 = 1 && r1 = 1`
- 不应出现（违反因果性）

---

## 🔧 API 使用

### Python API

```python
from riscv_tso_model import TSOTestGenerator, MemoryModel

# 创建 TSO 生成器
tso_gen = TSOTestGenerator(MemoryModel.TSO)

# 生成 Store Buffering 测试
thread0, thread1 = tso_gen.generate_store_buffering_test(seed=42)

# 生成 C 代码
code = tso_gen.generate_c_code("My_Test", thread0, thread1, iterations=100)

# 保存文件
with open("my_test.c", "w") as f:
    f.write(code)
```

### 自定义指令序列

```python
from riscv_tso_model import Instruction, InstructionType

# 手动构建指令序列
thread0 = [
    Instruction(InstructionType.STORE, address=0, value=1),
    Instruction(InstructionType.FENCE_TSO),
    Instruction(InstructionType.LOAD, address=64)
]
```

---

## 📝 代码结构

```
contrib/genetic-evolution/
├── python/
│   ├── riscv_tso_model.py      # TSO 模型生成器（新增）
│   ├── genetic_algorithm.py     # 遗传算法
│   └── test_generator.py        # 原测试生成器
├── store_buffering_tso.c        # TSO 测试（生成）
├── store_buffering_rvwmo.c      # RVWMO 测试（生成）
├── message_passing_tso.c        # 消息传递测试（生成）
├── load_buffering_tso.c         # 加载缓冲测试（生成）
└── RISCV_TSO_README.md          # 本文档
```

---

## 🧬 与遗传算法集成

### 扩展适应度函数

TSO 模型的适应度评估应该包括：

1. **地址对覆盖率** (原有)
2. **TSO 违例检测** (新增)
3. **Fence 密度** (新增)

```python
def tso_fitness(test_result):
    # 基础覆盖率
    coverage = len(test_result['address_pairs']) / 100.0
    
    # TSO 违例奖励
    tso_violations = detect_tso_violations(test_result['trace'])
    violation_bonus = len(tso_violations) * 2.0
    
    # Fence 密度惩罚（太多 fence 降低测试质量）
    fence_ratio = test_result['fence_count'] / test_result['total_ops']
    fence_penalty = fence_ratio * 0.5 if fence_ratio > 0.3 else 0
    
    return coverage + violation_bonus - fence_penalty
```

### TSO 违例检测

关键模式：
- Store Buffering: 检测 `r0 = 0 && r1 = 0`
- Out-of-Order Load: 检测 load 重排序
- Write-Write Reordering: 检测 store 重排序

---

## 📖 RISC-V Ztso 扩展规范

### Fence.tso 语义

`fence.tso` 相当于 `fence w,w` + 部分 `fence r,rw`：

```
fence.tso = fence (r|w), (rw | w)
```

**保证**:
1. 所有 preceding loads → 所有 subsequent memory operations
2. 所有 preceding stores → 所有 subsequent memory operations
3. 但**不保证** loads → subsequent loads (可重排序)

### TSO 内存模型特性

| 重排序类型 | RVWMO | TSO |
|-----------|-------|-----|
| Load-Load | ✅ | ❌ |
| Load-Store | ✅ | ❌ |
| Store-Store | ✅ | ❌ |
| Store-Load | ✅ | ✅ (需要 fence.tso) |

---

## 🔍 验证方法

### 1. 使用 gem5

```bash
# TSO 模式
gem5 --model=tso test.elf

# RVWMO 模式
gem5 --model=rvwmo test.elf
```

### 2. 使用 QEMU

```bash
# QEMU 目前不区分 TSO/RVWMO，模拟的是 RVWMO
qemu-riscv64 test
```

### 3. 使用 rmem

```bash
# 使用 rmem 验证 TSO 行为
rmem -model tso -isa RISCV test.litmus
```

---

## 📚 参考资料

### RISC-V 规范

1. **RISC-V ISA Manual Volume 1**
   - Chapter 14: Memory Consistency Model (RVWMO)
   
2. **RISC-V Ztso Extension**
   - Ratified Extension: Total Store Ordering
   - https://github.com/riscv/riscv-isa-manual

### 相关论文

1. **Simplifying ARM Concurrency** (POPL 2018)
   - Christopher Pulte et al.
   - 类似的 TSO 分析方法
   
2. **A Better x86 Memory Model: x86-TSO** (TPHOLs 2009)
   - Scott Owens et al.
   - TSO 模型的形式化定义

---

## 🚧 已知限制

1. **编译器支持**
   - `fence.tso` 需要 GCC 12+ 或 Clang 15+
   - 旧版本需要手动汇编

2. **硬件支持**
   - 少数 RISC-V 处理器实现 Ztso 扩展
   - 大部分仅支持 RVWMO

3. **工具支持**
   - gem5: 支持 TSO 模拟
   - QEMU: 仅模拟 RVWMO
   - Spike: 不区分内存模型

---

## 📅 更新日志

### v1.0.0 (2026-03-31)
- ✅ 实现 TSO/RVWMO 双模型生成器
- ✅ 支持 Store Buffering, Message Passing, Load Buffering 测试
- ✅ 生成 fence.tso 指令
- ✅ 完整的 Python API
- ✅ 示例测试代码

---

## 💡 未来计划

1. **更多 Litmus 测试**
   - IRIW (Independent Reads of Independent Writes)
   - CoRR (Coherence Read-Read)
   - CoWR (Coherence Write-Read)

2. **自动违例检测**
   - 运行时 TSO 违例检查
   - 自动报告可疑行为

3. **性能分析**
   - TSO vs RVWMO 性能对比
   - Fence 开销测量

4. **集成到演化框架**
   - TSO 专用适应度函数
   - TSO 违例驱动的变异策略

---

**文档生成时间**: 2026-03-31 23:34 GMT+8
**版本**: 1.0.0
**作者**: mc2lib TSO Extension
