# 🎉 遗传算法集成 - 第一阶段完成！

## ✅ 已完成的工作

### 1. 项目结构
```
mc2lib-evolution/
├── python/
│   └── evolution_controller.py    ✅ 演化控制器（已实现）
├── logs/
│   ├── evolution.log              ✅ 演化日志
│   ├── evolution_state_gen0.json  ✅ 第 0 代状态
│   ├── evolution_state_gen1.json  ✅ 第 1 代状态
│   └── evolution_state_gen2.json  ✅ 第 2 代状态
├── tests/                          (待创建)
├── config/                         (待创建)
└── results/                        (待创建)
```

### 2. 核心功能

#### ✅ 演化状态管理
- [x] 加载/保存演化状态
- [x] 跨代状态持久化
- [x] 支持从任意代恢复

#### ✅ 种群管理
- [x] 初始种群生成
- [x] 精英保留策略
- [x] 种群统计信息

#### ✅ 演化循环
- [x] 多代演化控制
- [x] 每代流程管理（生成→编译→运行→评估）
- [x] 日志记录

### 3. 测试结果

运行了 3 代演化（占位符实现）：
```
Generation 0: Best fitness: 0.9741, Avg: 0.5015
Generation 1: Best fitness: 0.9590, Avg: 0.5032
Generation 2: Best fitness: 0.9934, Avg: 0.5341

Total tests run: 150 (50 per generation)
```

---

## 📊 状态文件示例

```json
{
  "generation": 2,
  "population_size": 50,
  "best_fitness": 0.9934,
  "avg_fitness": 0.5341,
  "fitaddrs": [
    ["0x0", "0x40"],
    ["0x40", "0x80"],
    ...
  ],
  "tests": [
    {
      "test_id": "gen2_test_0",
      "generation": 2,
      "parent_ids": ["gen0_test_41"],
      "genome_data": "{...}",
      "fitness": 0.3139,
      "coverage": {
        "address_pairs": [[...]],
        "new_pairs": [[]],
        "violations": []
      }
    },
    ...
  ]
}
```

---

## 🚀 下一步实现计划

### 阶段 2: gem5 运行器（优先级最高）
```python
class GEM5Runner:
    def run_test(self, test_binary: str) -> TestResult
    def run_parallel(self, tests: List[str]) -> List[TestResult]
    def parse_gem5_output(self, log_path: str) -> Dict
```

**关键点**:
- 调用 gem5 运行编译好的测试
- 解析内存事件日志（CSV）
- 提取地址对和违例信息
- 支持并行运行多个 gem5 实例

### 阶段 3: 适应度评估器
```python
class FitnessEvaluator:
    def evaluate(self, test_result: TestResult, 
                 current_fitaddrs: Set) -> float
    def update_fitaddrs(self, results: List[TestResult]) -> Set
    def detect_violations(self, events: List[MemoryEvent]) -> List
```

**关键点**:
- 基于 consistency_checker.py 扩展
- 计算地址对覆盖率
- 奖励新发现的地址对
- 识别内存一致性违例

### 阶段 4: C++ 测试生成器
```cpp
class TestGenerator {
public:
    std::string generate_test(
        const TestGenome& genome,
        const std::set<AddressPair>& fitaddrs
    );
    
    std::string emit_c_code(const RandInstTest& test);
};
```

**关键点**:
- 基于 mc2lib RandInstTest
- 生成带事件记录的 C 代码
- 使用 RISC-V backend
- 嵌入 tracer.hpp

### 阶段 5: C++ 遗传算法引擎
```cpp
class GeneticEngine {
public:
    std::vector<TestGenome> evolve(
        const std::vector<TestGenome>& population,
        const EvolutionParams& params
    );
    
    // 使用 mcversi::CrossoverMutate
    CrossoverMutate<...> crossover_mutate_;
};
```

**关键点**:
- 包装 mcversi.hpp 的遗传算法
- 实现选择、交叉、变异
- 与 Python 控制器交互

---

## 🎯 完整工作流（目标）

```
第 N 代开始
    ↓
1. Python: 加载 evolution_state_gen{N-1}.json
    ↓
2. C++: 基于上一代 fitaddrs 生成新测试
    genome → RandInstTest → C code
    ↓
3. Shell: 编译测试
    riscv64-linux-gnu-gcc → binary
    ↓
4. Python: 在 gem5 上运行（并行）
    gem5.opt → memory_trace_core*.csv
    ↓
5. Python: 解析日志，评估适应度
    CSV → address_pairs → fitness
    ↓
6. Python: 更新 fitaddrs
    new_pairs → fitaddrs
    ↓
7. C++: 遗传算法（选择、交叉、变异）
    population → new_population
    ↓
8. Python: 保存 evolution_state_gen{N}.json
    ↓
第 N+1 代
```

---

## 📝 当前命令

### 运行演化（占位符）
```bash
cd /root/.openclaw/workspace/mc2lib-evolution
python3 python/evolution_controller.py --max-gen 10
```

### 从特定代恢复
```bash
python3 python/evolution_controller.py --start-gen 5 --max-gen 20
```

### 查看状态
```bash
cat logs/evolution_state_latest.json | python3 -m json.tool
```

### 查看日志
```bash
tail -f logs/evolution.log
```

---

## ⚙️ 配置参数（当前默认值）

```yaml
population_size: 50       # 种群大小
max_generations: 100      # 最大代数
mutation_rate: 0.1        # 变异率
elite_ratio: 0.1          # 精英比例
parallel_jobs: 4          # 并行任务数
gem5_timeout: 60          # gem5 超时（秒）

# 测试生成参数
min_threads: 2
max_threads: 2
min_ops_per_thread: 10
max_ops_per_thread: 30

# McVerSi 参数
P_USEL: 0.2              # 无条件选择概率
P_BFA: 0.05              # 从 fitaddr 构建概率
```

---

## 🎓 关键设计决策

### 1. 状态持久化
- 每代保存完整状态 JSON
- 支持断点续传
- 可追溯历史演化

### 2. Python + C++ 混合架构
- Python: 控制流、文件 I/O、并行调度
- C++: 测试生成、遗传算法（高性能）
- 通过 JSON 交换数据

### 3. fitaddr 驱动演化
- 跟踪所有发现的地址对
- 优先测试访问 fitaddr 的操作
- 逐代积累有趣的地址

### 4. 并行化策略
- gem5 实例并行运行
- 单机多核（4-8 核）
- 可扩展到多机集群

---

## 📊 性能估算

### 单机 (8 核)

| 阶段 | 单个测试 | 50 个测试（串行） | 50 个测试（并行） |
|------|---------|------------------|------------------|
| 生成 | <1s | ~1s | ~1s |
| 编译 | ~3s | ~150s | ~20s |
| gem5 | ~60s | ~3000s | ~400s |
| 评估 | <1s | ~1s | ~1s |
| **总计** | ~64s | **~52min** | **~7min** |

### 50 代演化
- 串行: ~43 小时
- 并行 (8 核): **~6 小时**

---

## 🎯 下一步行动

### 立即可做（1-2 小时）

1. **实现 gem5 运行器**
   ```python
   # 修改 evolution_controller.py 的 _run_tests_on_gem5
   def _run_tests_on_gem5(self, state):
       for test in state.tests:
           # 调用 gem5
           result = subprocess.run([...])
           # 解析日志
           events = parse_csv(f'memory_trace_{test.test_id}.csv')
   ```

2. **集成现有的 consistency_checker.py**
   ```python
   # 适应度评估
   from consistency_checker import analyze_trace
   fitness = calculate_fitness(events, state.fitaddrs)
   ```

3. **测试完整流程（使用现有的手工测试）**
   ```bash
   # 使用 riscv_multicore_percore 作为测试模板
   # 验证 gem5 运行 → 日志解析 → 适应度计算
   ```

### 短期目标（2-3 天）

4. **实现 C++ 测试生成器**
   - 基于 mc2lib RandInstTest
   - 生成可编译的 C 代码

5. **实现 C++ 遗传算法包装**
   - 包装 mcversi::CrossoverMutate
   - 与 Python 集成

### 中期目标（1 周）

6. **完整演化流程**
   - 端到端测试
   - 验证迭代正确性

7. **并行化和优化**
   - 多进程 gem5 运行
   - 缓存和增量编译

---

## 💬 需要确认的问题

1. **gem5 配置**：
   - 使用 AtomicSimpleCPU 还是 TimingSimpleCPU？
   - 2 核还是 4 核？
   - 超时设置多少秒？

2. **测试规模**：
   - 每个测试多少操作？(10-50)
   - 种群大小？(50 还是 100)
   - 演化代数？(50 还是 100)

3. **存储策略**：
   - 保存所有测试的二进制和日志？
   - 还是只保存最佳测试？

4. **优先级**：
   - 先实现完整流程（串行）？
   - 还是先优化性能（并行）？

---

## 📚 参考文件

- `/root/.openclaw/workspace/mc2lib-evolution/python/evolution_controller.py` - 演化控制器
- `/root/.openclaw/workspace/mc2lib/contrib/mcversi/consistency_checker.py` - 一致性检查器（可复用）
- `/root/.openclaw/workspace/mc2lib/contrib/mcversi/riscv_multicore_percore.c` - 测试模板
- `/root/.openclaw/workspace/gem5/configs/mc2lib_multicore_simple.py` - gem5 配置

---

🎉 **第一阶段基础设施已完成！准备继续下一步！** 🚀

你想先实现哪个部分？
1. gem5 运行器（最实用）
2. C++ 测试生成器（核心功能）
3. 适应度评估器（分析能力）

请告诉我优先级！
