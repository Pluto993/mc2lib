#!/usr/bin/env python3
"""
QEMU TSO/RVWMO 测试验证器
用于检查 QEMU 执行结果,识别内存模型违例,并用于迭代演化
"""

import re
import sys
import json
from collections import defaultdict
from typing import List, Dict, Tuple, Set

class MemoryEvent:
    def __init__(self, timestamp: int, seq_id: int, core_id: int, 
                 event_type: str, address: int, value: int, po_index: int):
        self.timestamp = timestamp
        self.seq_id = seq_id
        self.core_id = core_id
        self.type = event_type
        self.address = address
        self.value = value
        self.po_index = po_index
    
    def __repr__(self):
        return f"[Core{self.core_id}@{self.timestamp}] {self.type} 0x{self.address:x}={self.value}"

class TraceAnalyzer:
    """分析 QEMU 内存 trace"""
    
    def __init__(self, trace_file: str):
        self.trace_file = trace_file
        self.events: List[MemoryEvent] = []
        self.violations: List[Dict] = []
        
    def parse_trace(self):
        """解析 TRACE 输出"""
        pattern = re.compile(
            r'TRACE:(\d+),(\d+),(\d+),(\w+),0x([0-9a-fA-F]+),(\d+),(\d+)'
        )
        
        with open(self.trace_file, 'r') as f:
            for line in f:
                match = pattern.search(line)
                if match:
                    event = MemoryEvent(
                        timestamp=int(match.group(1)),
                        seq_id=int(match.group(2)),
                        core_id=int(match.group(3)),
                        event_type=match.group(4),
                        address=int(match.group(5), 16),
                        value=int(match.group(6)),
                        po_index=int(match.group(7))
                    )
                    self.events.append(event)
        
        print(f"✅ 解析了 {len(self.events)} 个内存事件")
        return self.events
    
    def check_store_buffering_violation(self) -> List[Dict]:
        """
        检查 Store Buffering 违例
        模式: Core0: W(x,1) -> R(y,0)  &&  Core1: W(y,1) -> R(x,0)
        这在 TSO 下不应该发生(有 fence 的情况下)
        """
        violations = []
        
        # 按迭代分组事件
        iterations = defaultdict(lambda: {'core0': [], 'core1': []})
        for event in self.events:
            iter_id = event.po_index // 3  # 每次迭代3个操作
            iterations[iter_id][f'core{event.core_id}'].append(event)
        
        for iter_id, cores in iterations.items():
            if len(cores['core0']) < 3 or len(cores['core1']) < 3:
                continue
            
            # 提取每个 core 的 WRITE 和 READ 值
            c0_events = sorted(cores['core0'], key=lambda e: e.po_index)
            c1_events = sorted(cores['core1'], key=lambda e: e.po_index)
            
            # 假设模式: W -> FENCE -> R
            c0_write = next((e for e in c0_events if e.type == 'WRITE'), None)
            c0_read = next((e for e in c0_events if e.type == 'READ'), None)
            c1_write = next((e for e in c1_events if e.type == 'WRITE'), None)
            c1_read = next((e for e in c1_events if e.type == 'READ'), None)
            
            if all([c0_write, c0_read, c1_write, c1_read]):
                # Store Buffering 违例: 两个 core 都读到 0
                if c0_read.value == 0 and c1_read.value == 0:
                    violations.append({
                        'type': 'Store_Buffering',
                        'iteration': iter_id,
                        'core0_write': (c0_write.address, c0_write.value),
                        'core0_read': (c0_read.address, c0_read.value),
                        'core1_write': (c1_write.address, c1_write.value),
                        'core1_read': (c1_read.address, c1_read.value),
                        'timestamp_range': (
                            min(c0_write.timestamp, c1_write.timestamp),
                            max(c0_read.timestamp, c1_read.timestamp)
                        )
                    })
        
        self.violations = violations
        return violations
    
    def compute_coverage_metrics(self) -> Dict:
        """计算覆盖率指标"""
        address_pairs = set()
        read_addresses = set()
        write_addresses = set()
        
        for event in self.events:
            if event.type == 'READ':
                read_addresses.add(event.address)
            elif event.type == 'WRITE':
                write_addresses.add(event.address)
        
        # 地址对: 所有读写组合
        for r in read_addresses:
            for w in write_addresses:
                if r != w:
                    address_pairs.add((r, w))
        
        return {
            'total_events': len(self.events),
            'read_count': sum(1 for e in self.events if e.type == 'READ'),
            'write_count': sum(1 for e in self.events if e.type == 'WRITE'),
            'fence_count': sum(1 for e in self.events if e.type == 'FENCE'),
            'unique_addresses': len(read_addresses | write_addresses),
            'address_pairs': len(address_pairs),
        }
    
    def generate_report(self, model_name: str) -> Dict:
        """生成完整报告"""
        self.parse_trace()
        violations = self.check_store_buffering_violation()
        metrics = self.compute_coverage_metrics()
        
        report = {
            'model': model_name,
            'trace_file': self.trace_file,
            'metrics': metrics,
            'violations': violations,
            'violation_count': len(violations),
        }
        
        return report
    
    def print_report(self, report: Dict):
        """打印报告"""
        print(f"\n{'='*60}")
        print(f"📊 {report['model']} 内存模型测试报告")
        print(f"{'='*60}")
        
        print(f"\n📈 覆盖率指标:")
        for key, value in report['metrics'].items():
            print(f"  {key:20s}: {value}")
        
        print(f"\n🚨 违例检测:")
        print(f"  Store Buffering 违例: {report['violation_count']} 次")
        
        if report['violations']:
            print(f"\n🔍 违例详情 (前5个):")
            for i, v in enumerate(report['violations'][:5], 1):
                print(f"\n  [{i}] 迭代 {v['iteration']}")
                print(f"      Core0: W{v['core0_write']} -> R{v['core0_read']}")
                print(f"      Core1: W{v['core1_write']} -> R{v['core1_read']}")
                print(f"      时间戳: {v['timestamp_range'][0]} - {v['timestamp_range'][1]}")

def compare_tso_vs_rvwmo(tso_trace: str, rvwmo_trace: str):
    """对比 TSO 和 RVWMO 的行为差异"""
    print(f"\n🔬 对比 TSO vs RVWMO 内存模型\n")
    
    tso_analyzer = TraceAnalyzer(tso_trace)
    tso_report = tso_analyzer.generate_report("RISC-V TSO")
    tso_analyzer.print_report(tso_report)
    
    rvwmo_analyzer = TraceAnalyzer(rvwmo_trace)
    rvwmo_report = rvwmo_analyzer.generate_report("RISC-V RVWMO")
    rvwmo_analyzer.print_report(rvwmo_report)
    
    # 差异分析
    print(f"\n{'='*60}")
    print(f"🆚 差异对比")
    print(f"{'='*60}")
    
    print(f"\n违例数差异:")
    print(f"  TSO:   {tso_report['violation_count']} 次")
    print(f"  RVWMO: {rvwmo_report['violation_count']} 次")
    print(f"  差值:  {rvwmo_report['violation_count'] - tso_report['violation_count']} 次")
    
    if tso_report['violation_count'] > 0:
        print(f"\n⚠️  TSO 出现违例 - fence.tso 可能未正确实现!")
    else:
        print(f"\n✅ TSO 无违例 - 内存顺序正确")
    
    if rvwmo_report['violation_count'] > tso_report['violation_count']:
        print(f"✅ RVWMO 允许更多重排序 - 符合预期的弱内存模型")
    
    # 保存结果
    result = {
        'tso': tso_report,
        'rvwmo': rvwmo_report,
        'comparison': {
            'violation_diff': rvwmo_report['violation_count'] - tso_report['violation_count'],
            'tso_correct': tso_report['violation_count'] == 0,
        }
    }
    
    with open('comparison_result.json', 'w') as f:
        json.dump(result, f, indent=2)
    
    print(f"\n💾 结果已保存到 comparison_result.json")
    
    return result

if __name__ == '__main__':
    if len(sys.argv) < 3:
        print("用法:")
        print(f"  {sys.argv[0]} <tso_trace.txt> <rvwmo_trace.txt>")
        print(f"\n或单独分析:")
        print(f"  {sys.argv[0]} <trace.txt>")
        sys.exit(1)
    
    if len(sys.argv) == 2:
        # 单独分析
        analyzer = TraceAnalyzer(sys.argv[1])
        report = analyzer.generate_report("Unknown")
        analyzer.print_report(report)
    else:
        # 对比分析
        compare_tso_vs_rvwmo(sys.argv[1], sys.argv[2])
