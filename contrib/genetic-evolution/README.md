# 🧬 mc2lib Genetic Evolution Framework

使用遗传算法在 gem5 上自动演化内存一致性测试的完整框架。

## 📋 项目概述

本项目实现了基于遗传算法的内存一致性测试自动生成和演化系统，能够：
- 🤖 自动生成 RISC-V 内存测试程序
- 🚀 在 gem5 模拟器上并行运行
- 📊 评估测试的地址覆盖率和违例发现能力
- 🧬 通过遗传算法不断演化和优化测试

## 🎯 核心功能

### 1. 遗传算法引擎
- 选择策略：锦标赛选择 + 无条件选择（P_USEL）
- 交叉操作：单点交叉混合父代操作序列
- 变异操作：5 种变异类型（地址、操作、交换、插入、删除）
- 精英保留：保留最佳个体到下一代
- fitaddr 驱动：引导演化探索有趣的地址对

### 2. gem5 集成
- 自动编译 RISC-V 测试程序
- 并行运行多个 gem5 实例（4 个 worker）
- 收集内存事件日志（CSV 格式）
- 超时和错误处理

### 3. 适应度评估
- 基础覆盖率：地址对数量
- 新地址对奖励：鼓励探索
- 违例发现奖励：检测内存一致性问题
- 超时/错误惩罚

## 📁 文件结构

```
genetic-evolution/
├── python/
│   ├── evolution_controller.py    # 演化主控制器
│   ├── genetic_algorithm.py       # 遗传算法引擎
│   ├── gem5_runner.py             # gem5 运行器
│   ├── fitness_evaluator.py       # 适应度评估器
│   └── test_generator.py          # 测试生成器（旧版）
├── logs/
│   ├── evolution.log              # 演化日志
│   └── evolution_state_*.json     # 每代状态快照
├── monitor.sh                     # 实时监控脚本
├── postprocess.py                 # 后处理脚本
├── test_pipeline.sh               # 完整流程测试
├── FINAL_SUMMARY.md               # 完整总结
├── RUNNING.md                     # 运行指南
└── README.md                      # 本文档
```

## 🚀 快速开始

### 环境要求

- Python 3.7+
- gem5 (RISCV 构建)
- riscv64-linux-gnu-gcc (交叉编译器)

### 安装

```bash
# 克隆仓库
git clone https://github.com/Pluto993/mc2lib.git
cd mc2lib/contrib/genetic-evolution

# 安装依赖（无特殊依赖，使用标准库）
```

### 运行演化

```bash
# 运行 10 代演化
python3 python/evolution_controller.py --max-gen 10

# 从第 5 代继续
python3 python/evolution_controller.py --start-gen 5 --max-gen 20

# 实时监控
./monitor.sh

# 查看日志
tail -f logs/evolution.log
```

## 📊 演化参数

**默认配置**:
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

## 🧬 遗传算法工作流程

```
Generation N:
  1. 生成种群 (随机或遗传演化)
     ↓
  2. 编译为 RISC-V 二进制 (gcc)
     ↓
  3. 在 gem5 上并行运行
     ↓
  4. 收集内存事件日志
     ↓
  5. 评估适应度
     ↓
  6. 更新 fitaddrs
     ↓
  7. 选择、交叉、变异
     ↓
Generation N+1
```

## 📈 适应度函数

```python
fitness = base_coverage + new_pair_bonus + violation_bonus - penalty

其中:
  base_coverage = num_address_pairs / 100
  new_pair_bonus = num_new_pairs × 0.1
  violation_bonus = num_violations × 0.2
  penalty = 0.5 (超时) 或 1.0 (错误)
```

## 🎯 实验结果

**2 代演化示例**:
```
Generation 0 (随机):
  - 最佳适应度: 2.3100
  - 平均适应度: 0.3150
  - 发现地址对: 21 个
  - 内存事件数: 2,400 个

Generation 1 (演化):
  - 最佳适应度: 2.3100 (保持)
  - 平均适应度: 0.3150 (保持)
  - 发现地址对: 21 个
  - 内存事件数: 4,800 个
  - 精英保留: gen0_test_0
```

**性能数据**:
- 单代时间: ~6-8 分钟 (20 测试, 4 并行)
- 每测试: ~20-30 秒
- 内存事件: ~120 事件/测试
- 地址覆盖: 21 个地址对

## 🔍 监控和分析

### 实时监控
```bash
./monitor.sh
```

### 查看演化趋势
```bash
python3 << 'EOF'
import json
from pathlib import Path

for f in sorted(Path('logs').glob('evolution_state_gen*.json')):
    with open(f) as fp:
        s = json.load(fp)
    print(f"Gen {s['generation']}: Best={s['best_fitness']:.4f}, "
          f"Avg={s['avg_fitness']:.4f}, Fitaddrs={len(s['fitaddrs'])}")
EOF
```

### 查看最佳测试
```bash
cat logs/evolution_state_latest.json | python3 -m json.tool | grep -A5 '"test_id"'
```

## 🎓 设计亮点

### 1. McVerSi 策略
- **P_USEL (0.2)**: 无条件选择保持种群多样性
- **P_BFA (0.05)**: 从 fitaddr 构建引导演化
- **精英保留**: 确保适应度不下降

### 2. 并行化
- gem5 实例并行运行（ProcessPoolExecutor）
- 4 个 worker 并发，加速 4 倍
- 自动负载均衡

### 3. 状态持久化
- JSON 格式，人类可读
- 断点续传支持
- 历史可追溯

### 4. 模块化设计
- 每个组件独立测试
- 清晰的接口
- 易于扩展

## 📚 相关文档

- `FINAL_SUMMARY.md` - 完整实现总结
- `RUNNING.md` - 详细运行指南
- `GENETIC_INTEGRATION_PLAN.md` - 设计文档
- `COMPLETE.md` - 组件说明

## 🤝 贡献

欢迎贡献！可以：
- 提交 Issue 报告问题
- 提交 Pull Request 改进代码
- 分享你的演化结果

## 📄 许可证

遵循 mc2lib 主仓库的许可证。

## 🙏 致谢

- 基于 [mc2lib](https://github.com/daniellustig/mc2lib) 和 McVerSi 论文
- gem5 模拟器
- OpenClaw AI 辅助开发

## 📞 联系

- GitHub: [Pluto993/mc2lib](https://github.com/Pluto993/mc2lib)
- Issues: [提交问题](https://github.com/Pluto993/mc2lib/issues)

---

**🎊 完整的遗传算法演化框架，用于自动生成和优化内存一致性测试！** 🚀
