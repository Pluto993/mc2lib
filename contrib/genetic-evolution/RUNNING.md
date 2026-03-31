# 🚀 mc2lib 遗传算法演化 - 运行指南

## 📊 当前状态

**运行中**: ✅ Generation 0-2 演化中
**预计时间**: 约 12 分钟 (2 代 × 6 分钟/代)
**工作目录**: `/root/.openclaw/workspace/mc2lib-evolution`

---

## 🎯 正在做什么

### 使用遗传算法在 gem5 上运行 mc2lib 内存一致性测试

**演化流程**:
```
Generation 0:
  1. 遗传算法生成 20 个随机测试
  2. 编译为 RISC-V 二进制 (gcc)
  3. 在 gem5 上运行 (并行 4 个)
  4. 收集内存事件日志
  5. 评估适应度
  6. 更新 fitaddrs

Generation 1:
  1. 使用遗传算法演化种群
     - 选择最佳个体
     - 交叉产生子代
     - 变异探索新空间
  2. 编译新测试
  3. gem5 运行
  4. 评估和更新
  
... 持续演化
```

---

## 📁 文件结构

```
mc2lib-evolution/
├── python/
│   ├── evolution_controller.py   ← 演化主控制器
│   ├── genetic_algorithm.py      ← 遗传算法引擎
│   ├── gem5_runner.py            ← gem5 运行器
│   └── fitness_evaluator.py     ← 适应度评估
├── tests/
│   └── generation_0/             ← 第 0 代测试
│       ├── gen0_test_0.c         ← 生成的 C 代码
│       ├── gen0_test_0           ← 编译的二进制
│       └── ...
├── runs/
│   └── gen0_test_0/              ← gem5 运行目录
│       ├── m5out/                ← gem5 输出
│       └── memory_trace_core*.csv ← 内存事件日志
├── logs/
│   ├── evolution_run.log         ← 完整日志
│   └── evolution_state_gen*.json ← 每代状态
└── monitor.sh                    ← 监控脚本
```

---

## 🔍 监控演化进度

### 方法 1: 使用监控脚本
```bash
cd /root/.openclaw/workspace/mc2lib-evolution
./monitor.sh
```

**输出示例**:
```
========================================
遗传算法演化实时监控
========================================

✅ 演化正在运行中

📊 最新状态:
----------------------------------------
  Generation:     0
  Best Fitness:   0.1234
  Avg Fitness:    0.0567
  Fitaddrs:       5
  Tests Run:      20
  Last Update:    2026-03-31T21:05:00
----------------------------------------

📈 演化趋势:
----------------------------------------
Gen | Best Fit | Avg Fit | Fitaddrs | Tests
----|----------|---------|----------|-------
  0 |   0.1234 |  0.0567 |        5 |    20
  1 |   0.2345 |  0.1234 |       12 |    40
----------------------------------------
```

### 方法 2: 实时查看日志
```bash
tail -f logs/evolution_run.log
```

### 方法 3: 查看状态文件
```bash
cat logs/evolution_state_latest.json | python3 -m json.tool
```

---

## 📊 遗传算法参数

**当前配置**:
```yaml
population_size: 20        # 种群大小
elite_ratio: 0.1           # 精英保留比例 (10%)
mutation_rate: 0.1         # 变异率 (10%)
P_USEL: 0.2               # 无条件选择概率
P_BFA: 0.05               # 从 fitaddr 构建概率

# 测试参数
num_threads: 2            # 线程数
ops_per_thread: 5-10      # 每线程操作数
num_iterations: 10        # 迭代次数

# gem5 配置
parallel_jobs: 4          # 并行运行数
timeout: 60s              # 单个测试超时
```

---

## 🧬 遗传算法工作原理

### 1. 初始种群生成
- 随机生成 20 个测试
- 每个测试包含 2 个线程的操作序列
- 操作类型: WRITE, READ, FENCE
- 地址: 0, 64, 128, ... 960 (64字节对齐)

### 2. 选择 (Selection)
- **锦标赛选择**: 从 3 个个体中选最优
- **无条件选择**: 20% 概率随机选择（保持多样性）

### 3. 交叉 (Crossover)
- 单点交叉
- 混合父代的操作序列

### 4. 变异 (Mutation)
- 5 种变异类型:
  - 改变地址
  - 改变操作类型
  - 交换顺序
  - 插入新操作
  - 删除操作
- 10% 概率变异

### 5. 精英保留
- 保留最佳 10% 个体
- 确保适应度不下降

### 6. fitaddr 驱动
- 跟踪有趣的地址对
- 5% 概率从 fitaddr 选择地址
- 引导演化探索有价值区域

---

## 📈 适应度函数

```python
fitness = base_coverage + new_pair_bonus + violation_bonus - penalty

其中:
  base_coverage = num_address_pairs / 100
  new_pair_bonus = num_new_pairs × 0.1
  violation_bonus = num_violations × 0.2
  penalty = 0.5 (超时) 或 1.0 (错误)
```

**目标**: 最大化地址对覆盖率和违例发现

---

## ⏱️ 性能预估

**单代时间**:
- 生成测试: <1 秒
- 编译: ~60 秒 (20 测试 × 3秒)
- gem5 运行: ~300 秒 (20 测试 ÷ 4 并行 × 60秒)
- 评估: <1 秒
- **总计: ~6 分钟/代**

**2 代演化**:
- 预计时间: ~12 分钟
- 总测试数: 40 个
- 期望 fitaddrs: 10-20 个

---

## 🎯 预期结果

### Generation 0 (随机初始化)
- Best Fitness: ~0.01-0.10
- Avg Fitness: ~0.005-0.05
- Fitaddrs: 5-15 个
- Address Pairs: 每个测试 1-5 对

### Generation 1 (演化后)
- Best Fitness: ~0.10-0.20 (+100%↑)
- Avg Fitness: ~0.05-0.10 (+100%↑)
- Fitaddrs: 10-30 个 (+100%↑)
- Address Pairs: 每个测试 3-10 对

---

## 🔧 命令速查

### 查看进度
```bash
./monitor.sh
```

### 实时日志
```bash
tail -f logs/evolution_run.log
```

### 查看 gem5 运行
```bash
ps aux | grep gem5
```

### 查看测试文件
```bash
ls -lh tests/generation_0/
```

### 查看内存日志
```bash
head tests/generation_0/memory_trace_core0.csv
```

### 停止演化
```bash
# 优雅停止（会保存状态）
pkill -INT -f evolution_controller

# 强制停止
pkill -9 -f evolution_controller
```

### 继续演化
```bash
# 从上次停止的地方继续
python3 python/evolution_controller.py --start-gen N --max-gen M
```

---

## 📊 结果分析

### 查看所有代的趋势
```bash
python3 << 'EOF'
import json
from pathlib import Path

states = []
for f in sorted(Path('logs').glob('evolution_state_gen*.json')):
    with open(f) as fp:
        states.append(json.load(fp))

print("Gen | Best | Avg  | Fitaddrs | Improvement")
print("----|------|------|----------|-------------")
for i, s in enumerate(states):
    imp = ""
    if i > 0:
        prev = states[i-1]
        delta = s['best_fitness'] - prev['best_fitness']
        imp = f"+{delta:.4f} ({delta/prev['best_fitness']*100:.1f}%)"
    print(f"{s['generation']:3d} | {s['best_fitness']:.4f} | "
          f"{s['avg_fitness']:.4f} | {len(s['fitaddrs']):8d} | {imp}")
EOF
```

### 查看最佳测试
```bash
python3 << 'EOF'
import json

with open('logs/evolution_state_latest.json') as f:
    state = json.load(f)

# 找到最佳测试
tests = state['tests']
best_test = max(tests, key=lambda t: t['fitness'])

print(f"Best Test: {best_test['test_id']}")
print(f"Fitness: {best_test['fitness']:.4f}")
print(f"Generation: {best_test['generation']}")
print(f"Parents: {best_test.get('parent_ids', [])}")
print(f"Operations:")
for thread_id, ops in best_test['operations'].items():
    print(f"  Thread {thread_id}: {len(ops)} ops")
    print(f"    Sample: {ops[:3]}")
EOF
```

---

## 🎉 成功标志

演化成功的指标:
- ✅ Best Fitness 逐代增长
- ✅ Fitaddrs 数量增加
- ✅ 发现内存一致性违例
- ✅ Address Pairs 覆盖率提升

---

## 💡 故障排查

### 问题: 所有测试 fitness 为 0
**原因**: 内存日志未生成
**解决**: 检查 `runs/gen*_test_*/memory_trace_core*.csv`

### 问题: gem5 超时
**原因**: 测试操作过多或死锁
**解决**: 减少 `max_ops_per_thread` 或增加 `timeout`

### 问题: 编译失败
**原因**: 生成的 C 代码有语法错误
**解决**: 检查 `tests/generation_*/gen*.c`

---

## 📞 当前运行状态

**进程 ID**: 查看 `ps aux | grep evolution`
**日志文件**: `logs/evolution_run.log`
**预计完成**: 约 12 分钟后 (21:12 左右)

---

**🎊 遗传算法正在 gem5 上演化 mc2lib 测试！请稍候查看结果！** 🚀
