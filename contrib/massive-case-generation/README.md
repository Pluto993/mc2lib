# 超大规模内存一致性测试生成器

基于 `qemu-evolution` 框架的增强版本，专门用于生成**超大规模**的 RISC-V 内存一致性测试用例。

---

## 📊 规模对比

| 项目 | qemu-evolution | **massive-case-generation** |
|------|----------------|----------------------------|
| 迭代次数 | 100 | **10,000 - 500,000+** |
| 事件数 | 2,000 | **100,000 - 5,000,000+** |
| 地址数 | 10 | **100 - 10,000+** |
| 测试数 | 3 | **5 - 100+** |
| 模式 | 简单 | **复杂（SB/MP/LB/RMW/循环/依赖链）** |

---

## 🎯 设计目标

1. **深度探索** - 通过大量迭代发现罕见的内存一致性行为
2. **广度覆盖** - 使用数千个地址覆盖大片内存空间
3. **复杂模式** - 支持多种经典 Litmus 测试和自定义模式
4. **可扩展** - 5 种预设规模 + 灵活配置

---

## 🚀 快速开始

### 1. 生成测试

```bash
cd /root/.openclaw/workspace/mc2lib/contrib/massive-case-generation

# 生成中等规模测试（默认）
python3 generate_massive_tests.py

# 生成大规模测试
python3 generate_massive_tests.py --scale large --pattern mixed

# 生成极限规模测试
python3 generate_massive_tests.py --scale extreme --pattern random
```

### 2. 编译测试

```bash
# 编译所有生成的测试
for test in massive_test_*.c; do
    name=${test%.c}
    echo "Compiling $name..."
    riscv64-linux-gnu-gcc -static -O2 -o $name $test
done
```

### 3. 运行测试

```bash
# 运行所有测试
for test in massive_test_medium_*; do
    [ -x "$test" ] && echo "Running $test..." && qemu-riscv64 ./$test > ${test}_log.txt
done
```

### 4. 分析结果

```bash
python3 analyze_massive_results.py
```

---

## 📐 规模模式

### Small（小型）
- 迭代: 1,000
- 地址: 100
- 事件: 10,000
- 用途: 快速验证

### Medium（中型，默认）
- 迭代: 10,000
- 地址: 500
- 事件: 100,000
- 用途: 常规测试

### Large（大型）
- 迭代: 50,000
- 地址: 2,000
- 事件: 500,000
- 用途: 深度探索

### Massive（超大）
- 迭代: 100,000
- 地址: 5,000
- 事件: 1,000,000
- 用途: 压力测试

### Extreme（极限）
- 迭代: 500,000
- 地址: 10,000
- 事件: 5,000,000
- 用途: 极限探索

---

## 🧬 测试模式

### 1. Store Buffering (sb)
经典弱内存测试，检测 store buffer 效应。

```
Thread 0: x=1; FENCE; r0=y
Thread 1: y=1; FENCE; r1=x

问题: r0==0 && r1==0 是否可能？
```

### 2. Message Passing (mp)
检测写操作的传播顺序。

```
Thread 0: data=1; FENCE; flag=1
Thread 1: r0=flag; FENCE; r1=data

问题: r0==1 && r1==0 是否可能？
```

### 3. Random（随机）
完全随机的 READ/WRITE/FENCE/RMW 操作。

### 4. Loop Intensive（循环密集）
对少量热点地址进行高频操作。

### 5. RMW Heavy（原子操作密集）
大量使用 AMOSWAP 等原子操作。

### 6. Address Dependency（地址依赖链）
构建长依赖链: read(a) → write(b) → read(c) → ...

### 7. Mixed（混合）
混合以上所有模式。

---

## 🔧 命令行参数

```bash
python3 generate_massive_tests.py [OPTIONS]

Options:
  --scale {small,medium,large,massive,extreme}
          测试规模 (默认: medium)
  
  --pattern {sb,mp,lb,random,loop,rmw,addr_dep,mixed}
          测试模式 (默认: mixed)
  
  --output-dir DIR
          输出目录 (默认: 当前目录)
```

### 示例

```bash
# 生成 20 个大规模 Store Buffering 测试
python3 generate_massive_tests.py --scale large --pattern sb

# 生成 100 个极限规模混合模式测试
python3 generate_massive_tests.py --scale extreme --pattern mixed

# 生成小型随机测试到指定目录
python3 generate_massive_tests.py --scale small --pattern random --output-dir ./tests/
```

---

## 📊 输出格式

### C 源文件

```
massive_test_{scale}_{pattern}_{id}.c
```

示例:
- `massive_test_medium_mixed_0.c`
- `massive_test_large_sb_5.c`
- `massive_test_extreme_random_42.c`

### 追踪日志

```
TRACE:<timestamp>,<seq_id>,<core_id>,<type>,<address>,<value>,<po_index>
```

类型:
- `WRITE` - 写操作
- `READ` - 读操作
- `FENCE` - 内存屏障
- `RMW` - 原子 Read-Modify-Write

---

## 💾 内存需求

| 规模 | 源文件 | 编译后 | 日志文件 | 总计 |
|------|--------|--------|----------|------|
| Small | ~50KB | ~600KB | ~1MB | ~2MB |
| Medium | ~200KB | ~800KB | ~10MB | ~11MB |
| Large | ~500KB | ~1MB | ~50MB | ~51MB |
| Massive | ~1MB | ~1.5MB | ~100MB | ~102MB |
| Extreme | ~2MB | ~2MB | ~500MB | ~504MB |

**注意**: Extreme 模式生成的日志可能达到 GB 级别。

---

## ⚡ 性能

### 生成速度

| 规模 | 生成时间 | 编译时间 | 运行时间 |
|------|----------|----------|----------|
| Small | < 1s | ~5s | ~2s |
| Medium | ~2s | ~10s | ~10s |
| Large | ~5s | ~15s | ~60s |
| Massive | ~10s | ~30s | ~300s |
| Extreme | ~30s | ~60s | ~1500s |

*运行时间基于 QEMU 用户模式 (qemu-riscv64)*

---

## 🎓 与 qemu-evolution 的区别

### qemu-evolution（原始版本）
- ✅ 轻量级，快速原型
- ✅ 适合初步探索
- ❌ 规模有限
- ❌ 模式简单

### massive-case-generation（本版本）
- ✅ 超大规模（100x - 1000x 事件数）
- ✅ 多种复杂测试模式
- ✅ 支持原子操作 (RMW)
- ✅ 灵活的配置系统
- ✅ 进度显示和性能统计
- ❌ 生成和运行时间更长
- ❌ 需要更多内存

---

## 🔬 使用场景

### 1. 发现罕见竞态条件
使用 Massive/Extreme 规模 + 大量迭代。

### 2. 压力测试模拟器
验证 QEMU/gem5 在大规模测试下的稳定性。

### 3. 内存模型研究
使用 Store Buffering/Message Passing 等模式深入研究 RVWMO。

### 4. 性能基准测试
使用 Loop Intensive 模式测试缓存行为。

### 5. 原子操作验证
使用 RMW Heavy 模式测试 AMOSWAP 等指令。

---

## 📝 TODO

- [ ] 多核并行测试生成
- [ ] 遗传算法自动优化
- [ ] 与 gem5 集成
- [ ] 图形化结果分析
- [ ] 支持更多 RISC-V 原子指令
- [ ] CSV/JSON 格式日志输出

---

## 🐛 已知问题

1. **QEMU 单核限制**: QEMU 用户模式不支持真正的多核并发
2. **内存占用**: Extreme 模式可能需要 > 4GB RAM
3. **时间戳精度**: rdcycle 在 QEMU 中不精确

---

## 📚 参考资料

- [qemu-evolution](../qemu-evolution/README.md)
- [RISC-V Memory Model Spec](https://riscv.org/technical/specifications/)
- [Litmus Tests](https://www.cl.cam.ac.uk/~pes20/weakmemory/)

---

## 📧 联系

GitHub: https://github.com/Pluto993/mc2lib

---

**最后更新**: 2026-04-10
**版本**: 1.0.0
**作者**: AI Assistant + laddlin
