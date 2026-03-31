#!/bin/bash
# 实时监控遗传算法演化进度

WORK_DIR="/root/.openclaw/workspace/mc2lib-evolution"

echo "========================================"
echo "遗传算法演化实时监控"
echo "========================================"
echo ""

# 检查是否在运行
if pgrep -f "evolution_controller.py" > /dev/null; then
    echo "✅ 演化正在运行中"
    echo ""
else
    echo "❌ 演化未运行"
    echo ""
fi

# 显示最新状态
if [ -f "$WORK_DIR/logs/evolution_state_latest.json" ]; then
    echo "📊 最新状态:"
    echo "----------------------------------------"
    python3 << 'PYEOF'
import json
import sys
sys.path.insert(0, '/root/.openclaw/workspace/mc2lib-evolution')

try:
    with open('logs/evolution_state_latest.json') as f:
        state = json.load(f)
    
    print(f"  Generation:     {state['generation']}")
    print(f"  Best Fitness:   {state['best_fitness']:.4f}")
    print(f"  Avg Fitness:    {state['avg_fitness']:.4f}")
    print(f"  Fitaddrs:       {len(state['fitaddrs'])}")
    print(f"  Tests Run:      {state['total_tests_run']}")
    print(f"  Last Update:    {state['last_update']}")
except Exception as e:
    print(f"  错误: {e}")
PYEOF
    echo "----------------------------------------"
    echo ""
fi

# 显示所有代的趋势
if ls "$WORK_DIR/logs/evolution_state_gen"*.json 2>/dev/null | grep -q .; then
    echo "📈 演化趋势:"
    echo "----------------------------------------"
    echo "Gen | Best Fit | Avg Fit | Fitaddrs | Tests"
    echo "----|----------|---------|----------|-------"
    python3 << 'PYEOF'
import json
from pathlib import Path
import sys
sys.path.insert(0, '/root/.openclaw/workspace/mc2lib-evolution')

states = []
for state_file in sorted(Path('logs').glob('evolution_state_gen*.json')):
    try:
        with open(state_file) as f:
            states.append(json.load(f))
    except:
        pass

for s in states:
    print(f"{s['generation']:3d} | {s['best_fitness']:8.4f} | "
          f"{s['avg_fitness']:7.4f} | {len(s['fitaddrs']):8d} | "
          f"{s['total_tests_run']:5d}")
PYEOF
    echo "----------------------------------------"
    echo ""
fi

# 显示最新日志（最后 10 行）
if [ -f "$WORK_DIR/logs/evolution_run.log" ]; then
    echo "📋 最新日志 (最后 10 行):"
    echo "----------------------------------------"
    tail -10 "$WORK_DIR/logs/evolution_run.log"
    echo "----------------------------------------"
    echo ""
fi

echo "💡 提示:"
echo "  实时日志: tail -f logs/evolution_run.log"
echo "  查看进程: ps aux | grep evolution"
echo "  停止演化: pkill -f evolution_controller"
echo ""
