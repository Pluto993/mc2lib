# 学习笔记：qemu-evolution 与 massive-case-generation

## 📚 qemu-evolution 学习总结

### 核心思想

qemu-evolution 是一个基于**遗传算法**的 RISC-V 内存一致性测试演化框架，使用 QEMU 用户模式实现快速迭代。

### 关键组件

#### 1. 第一代测试（基准）
`simple_mc2lib_test.c` - 简单的固定模式测试：
- 100 次迭代
- 3 个地址 (0, 64, 128)
- 简单的 WRITE/READ/FENCE 循环
- 生成 100 个事件

#### 2. 追踪格式
统一的 `TRACE:` 格式：
```
TRACE:<timestamp>,<seq_id>,<core_id>,<type>,<address>,<value>,<po_index>
```

#### 3. 遗传算法引擎
`evolve_gen2.py` - 核心演化逻辑：
- 解析第一代日志
- 提取操作模式
- 变异生成第二代：
  - `change_addr` - 改变地址
  - `change_type` - READ ↔ WRITE 互换
  - `insert` - 插入新操作
  - `delete` - 删除操作
- 变异率: 20-30%

#### 4. 适应度函数
```python
fitness = len(address_pairs) / 100.0 + len(new_pairs) * 0.1
```
- 基础覆盖率：地址对数量
- 新地址对奖励：探索奖励

#### 5. QEMU 执行
使用 `qemu-riscv64` 快速运行 RISC-V 静态链接二进制：
- 速度：秒级（vs gem5 的分钟级）
- 提速：100x
- 局限：单核模拟，不能测试真正的并发

### 文件结构

```
qemu-evolution/
├── simple_mc2lib_test.c       # 第一代基准测试
├── evolve_gen2.py              # 遗传算法演化器
├── analyze_evolution.py        # 结果分析工具
├── run_evolution.sh            # 一键运行脚本
├── gen2_test_*.c               # 第二代测试（生成的）
└── *_log.txt                   # 追踪日志
```

### 工作流程

```
第一代测试
    ↓
编译 & 运行 (QEMU)
    ↓
生成日志
    ↓
解析日志 → 提取模式
    ↓
遗传算法变异
    ↓
生成第二代测试
    ↓
编译 & 运行 & 分析
```

---

## 🚀 massive-case-generation 增强点

### 设计目标

将 qemu-evolution 的规模扩展 **100x - 1000x**，以发现更深层次的内存一致性问题。

### 主要改进

#### 1. 规模扩展（5 级）

| 级别 | 迭代 | 地址 | 事件 | 倍数 |
|------|------|------|------|------|
| qemu-evolution | 100 | 10 | 2K | 1x |
| **Small** | 1K | 100 | 10K | **5x** |
| **Medium** | 10K | 500 | 100K | **50x** |
| **Large** | 50K | 2K | 500K | **250x** |
| **Massive** | 100K | 5K | 1M | **500x** |
| **Extreme** | 500K | 10K | 5M | **2500x** |

#### 2. 复杂测试模式

qemu-evolution 只有简单的随机变异，massive-case-generation 支持：

##### Store Buffering (SB)
```
Thread 0: x=1; FENCE; r0=y
Thread 1: y=1; FENCE; r1=x
```

##### Message Passing (MP)
```
Thread 0: data=1; FENCE; flag=1
Thread 1: r0=flag; FENCE; r1=data
```

##### Loop Intensive
高频访问热点地址，测试缓存一致性。

##### RMW Heavy
大量使用 `AMOSWAP` 等原子操作。

##### Address Dependency
构建长依赖链测试地址依赖。

##### Mixed
混合所有模式。

#### 3. 高级特性

- ✅ **进度显示** - 实时显示测试进度
- ✅ **性能统计** - 运行时间、事件/秒
- ✅ **动态内存** - 使用 `malloc` 避免栈溢出
- ✅ **超时保护** - 防止测试卡死
- ✅ **批量分析** - 分析多个测试结果

#### 4. 配置系统

使用 `dataclass` 和 `enum` 提供类型安全的配置：

```python
@dataclass
class GenerationConfig:
    mode: ScaleMode
    num_iterations: int
    num_addresses: int
    max_events: int
    pattern_length: int
    num_tests: int
    mutation_rate: float
```

---

## 📊 对比表

| 特性 | qemu-evolution | massive-case-generation |
|------|----------------|-------------------------|
| **规模** | 固定小规模 | 5 级可选 (小→极限) |
| **迭代** | 100 | 1K - 500K |
| **地址** | 10 | 100 - 10K |
| **事件** | 2K | 10K - 5M |
| **模式** | 随机变异 | 7 种经典模式 |
| **原子操作** | ❌ | ✅ AMOSWAP |
| **进度显示** | ❌ | ✅ 实时进度 |
| **配置** | 硬编码 | ✅ 灵活配置 |
| **分析工具** | 简单 | ✅ 详细统计 |
| **文档** | 基础 | ✅ 完整文档 |

---

## 🎯 使用建议

### 何时使用 qemu-evolution

- ✅ 快速原型验证
- ✅ 学习遗传算法基础
- ✅ 资源受限环境
- ✅ 初步探索

### 何时使用 massive-case-generation

- ✅ 发现罕见竞态条件
- ✅ 压力测试模拟器
- ✅ 深入研究内存模型
- ✅ 大规模覆盖测试
- ✅ 性能基准测试

---

## 💡 最佳实践

### 1. 渐进式测试

```bash
# 从小规模开始
python3 generate_massive_tests.py --scale small --pattern random

# 验证正确性后，逐步增大
python3 generate_massive_tests.py --scale medium --pattern mixed

# 最终使用大规模
python3 generate_massive_tests.py --scale massive --pattern mixed
```

### 2. 模式选择

- **初步探索**: `random` - 完全随机，覆盖广
- **特定场景**: `sb` / `mp` / `lb` - 针对性测试
- **综合测试**: `mixed` - 混合所有模式
- **性能测试**: `loop` - 循环密集
- **原子操作**: `rmw` - 原子操作密集

### 3. 结果分析

```bash
# 运行测试
./run_massive.sh medium mixed

# 分析结果
python3 analyze_massive_results.py

# 查看最佳测试
grep "最佳结果" -A 10 分析输出
```

---

## 🔬 技术细节

### 内存布局

```
地址对齐到 64 字节（cache line）:
0x0, 0x40, 0x80, 0xC0, ...

shared_mem 大小 = num_addresses * 64
```

### RISC-V 指令

```c
// 读周期计数器
uint64_t rdcycle()

// 内存屏障
fence rw, rw

// 原子交换
amoswap.d rd, rs2, (rs1)
```

### 事件记录

```c
typedef struct {
    uint64_t timestamp;   // rdcycle 时间戳
    uint32_t seq_id;      // 全局序列号
    uint32_t core_id;     // 核心 ID (目前都是 0)
    uint32_t type;        // 0=WRITE, 1=READ, 2=FENCE, 3=RMW
    uint64_t address;     // 内存地址
    uint64_t value;       // 读写值
    uint32_t po_index;    // Program Order 索引
} MemoryEvent;
```

---

## 📝 总结

### qemu-evolution 的优点

- ✅ 简洁易懂
- ✅ 快速原型
- ✅ 基础遗传算法示例

### massive-case-generation 的优势

- ✅ 超大规模（100x - 2500x）
- ✅ 多种复杂模式
- ✅ 灵活配置系统
- ✅ 完整的工具链
- ✅ 详细的文档

### 学到的经验

1. **规模很重要** - 大量迭代能发现罕见问题
2. **模式多样性** - 不同模式测试不同行为
3. **工具化** - 自动化生成、编译、运行、分析
4. **QEMU 的价值** - 快速迭代，但不能替代 gem5
5. **追踪格式** - 统一格式便于分析

---

## 🚧 后续工作

1. **多核支持** - 集成真实的多核测试
2. **gem5 集成** - 在 gem5 上运行大规模测试
3. **自动分析** - 自动识别内存一致性违规
4. **可视化** - 图形化展示测试结果
5. **优化** - 减少内存占用和运行时间

---

**感谢 qemu-evolution 提供的基础框架！**
