# QEMU TSO/RVWMO 测试验证与迭代指南

本指南介绍如何使用 QEMU 对 RISC-V TSO/RVWMO 内存模型进行测试、验证和迭代优化。

## 📁 文件说明

| 文件 | 用途 |
|------|------|
| `qemu_tso_verifier.py` | 验证器 - 分析 QEMU trace,检测违例 |
| `qemu_test_pipeline.sh` | 自动化流程 - 编译→运行→验证 |
| `qemu_evolution_engine.py` | 演化引擎 - 遗传算法迭代优化测试 |
| `store_buffering_tso.c` | TSO 测试源码 (已修复按时间戳排序) |
| `store_buffering_rvwmo.c` | RVWMO 测试源码 |

## 🚀 快速开始

### 1. 运行单次测试

```bash
# 完整自动化流程 (编译 → 运行 → 对比)
bash qemu_test_pipeline.sh
```

**输出:**
- `test_tso`, `test_rvwmo` - RISC-V 可执行文件
- `tso_trace.txt`, `rvwmo_trace.txt` - 原始 trace
- `comparison_result.json` - 对比结果

### 2. 手动分析 Trace

```bash
# 单独分析某个 trace
python3 qemu_tso_verifier.py tso_trace.txt

# 对比两个 trace
python3 qemu_tso_verifier.py tso_trace.txt rvwmo_trace.txt
```

**分析内容:**
- ✅ 覆盖率指标 (地址对、事件数、fence 数)
- 🚨 Store Buffering 违例检测
- 🆚 TSO vs RVWMO 差异对比

### 3. 启动遗传算法演化

```bash
# 小规模测试 (10个体 × 5代)
python3 qemu_evolution_engine.py --population 10 --generations 5

# 大规模演化 (20个体 × 20代)
python3 qemu_evolution_engine.py --population 20 --generations 20 \
    --mutation-rate 0.3 --crossover-rate 0.7
```

**演化过程:**
1. **初始化** - 随机生成测试程序种群
2. **评估** - 编译→QEMU运行→计算适应度
3. **选择** - 锦标赛选择父代
4. **交叉** - 混合父代配置
5. **变异** - 随机改变迭代数/fence类型/地址
6. **迭代** - 重复 2-5

**适应度函数:**
```python
fitness = (address_pairs × 10) + (unique_addresses × 5) + (violations × 50)
```
- 地址覆盖越多 → 适应度越高
- 发现违例 → 巨额奖励(50分)
- 目标: 生成能触发内存模型边界行为的测试

## 📊 理解结果

### Store Buffering 违例

**违例模式:**
```
// 初始: x = 0, y = 0
Thread 0: W(x,1) → fence → R(y) = ?
Thread 1: W(y,1) → fence → R(x) = ?

结果: r0 = 0 && r1 = 0  <-- 违例!
```

**期望行为:**
- **TSO:** 不应该出现 `r0=0 && r1=0` (有 fence.tso)
- **RVWMO:** 允许 `r0=0 && r1=0` (弱内存模型)

### 验证器输出示例

```
============================================================
📊 RISC-V TSO 内存模型测试报告
============================================================

📈 覆盖率指标:
  total_events        : 600
  read_count          : 200
  write_count         : 200
  fence_count         : 200
  unique_addresses    : 2
  address_pairs       : 2

🚨 违例检测:
  Store Buffering 违例: 0 次

✅ TSO 无违例 - 内存顺序正确
```

### 对比结果

```json
{
  "tso": {
    "violation_count": 0,
    "metrics": { "address_pairs": 2, ... }
  },
  "rvwmo": {
    "violation_count": 3,
    "metrics": { "address_pairs": 2, ... }
  },
  "comparison": {
    "violation_diff": 3,
    "tso_correct": true
  }
}
```

**解读:**
- TSO 0次违例 → fence.tso 正确阻止了重排序 ✅
- RVWMO 3次违例 → 允许更多重排序,符合预期 ✅
- 差值 > 0 → TSO 比 RVWMO 更强 ✅

## 🔬 进阶使用

### 修改测试参数

编辑 C 源码:
```c
#define NUM_ITERATIONS 100  // 增加到 1000 提高触发概率
#define MEMORY_SIZE 1024    // 扩大内存范围
```

重新编译:
```bash
riscv64-linux-gnu-gcc -march=rv64gc_ztso -DRISCV_TSO -static -o test_tso store_buffering_tso.c
```

### 自定义演化策略

修改 `qemu_evolution_engine.py`:
```python
def evaluate_fitness(self, individual: TestProgram) -> float:
    # 自定义适应度函数
    coverage_score = metrics['address_pairs'] * 20  # 提高覆盖权重
    violation_bonus = violations * 100              # 提高违例奖励
    
    # 惩罚过长的测试
    time_penalty = individual.num_iterations / 1000 * (-5)
    
    return coverage_score + violation_bonus + time_penalty
```

### 多次运行取统计

```bash
# 运行10次,统计违例率
for i in {1..10}; do
    qemu-riscv64 test_rvwmo 2>&1 | \
        python3 qemu_tso_verifier.py /dev/stdin | \
        grep "违例:" >> stats.txt
done

# 分析结果
grep -oP "违例: \K\d+" stats.txt | \
    awk '{sum+=$1; count++} END {print "平均违例数:", sum/count}'
```

## 🐛 故障排查

### QEMU 找不到动态链接器

**错误:**
```
qemu-riscv64: Could not open '/lib/ld-linux-riscv64-lp64d.so.1'
```

**解决:**
```bash
# 使用静态链接
riscv64-linux-gnu-gcc ... -static -o test ...
```

### 编译时 fence.tso 不支持

**错误:**
```
Error: no such instruction: `fence.tso'
```

**解决:**
```bash
# 确保使用 Ztso 扩展
riscv64-linux-gnu-gcc -march=rv64gc_ztso ...
```

### Python 找不到 qemu_tso_verifier

**错误:**
```
ModuleNotFoundError: No module named 'qemu_tso_verifier'
```

**解决:**
```bash
# 确保在同一目录或设置 PYTHONPATH
cd mc2lib/contrib/genetic-evolution
python3 qemu_evolution_engine.py ...
```

## 📚 参考资料

- **RISC-V ISA Manual Vol 1, Chapter 14** - Memory Model
- **RISC-V Ztso Extension** - Total Store Ordering
- **mc2lib 原始论文** - Genetic Evolution for Memory Consistency Testing
- **QEMU RISC-V 文档** - https://www.qemu.org/docs/master/system/target-riscv.html

## 🎯 下一步

1. ✅ 验证基础 TSO/RVWMO 行为
2. 🧬 运行遗传算法找最优测试
3. 📊 收集大量统计数据
4. 🔬 尝试其他 Litmus 测试 (Message Passing, Load Buffering)
5. 🚀 集成到 CI/CD 流程

---

**需要帮助?** 运行 `python3 qemu_tso_verifier.py --help` 或查看源码注释。
