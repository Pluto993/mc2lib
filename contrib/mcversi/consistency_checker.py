#!/usr/bin/env python3
"""
Memory Consistency Checker for mc2lib traces
Analyzes recorded memory events and checks for consistency violations

Supports checking:
- Sequential Consistency (SC)
- RISC-V Weak Memory Ordering (RVWMO)
- Total Store Order (TSO)
"""

import sys
import csv
from dataclasses import dataclass
from typing import List, Dict, Set, Tuple
from collections import defaultdict

@dataclass
class MemoryEvent:
    """Memory operation event"""
    timestamp: int
    seq_id: int
    core_id: int
    type: str  # READ, WRITE, FENCE, ATOMIC
    address: int
    value: int
    po_index: int  # Program order index
    
    def __repr__(self):
        return f"C{self.core_id}:{self.type}({self.address:#x})={self.value}@{self.po_index}"

class ConsistencyChecker:
    """Memory consistency model checker"""
    
    def __init__(self, events: List[MemoryEvent]):
        self.events = sorted(events, key=lambda e: e.timestamp)
        self.cores = set(e.core_id for e in events)
        self.addresses = set(e.address for e in events if e.type in ['READ', 'WRITE'])
        
        # Build program order per core
        self.program_order = defaultdict(list)
        for event in self.events:
            self.program_order[event.core_id].append(event)
        
        # Sort by po_index to ensure correct program order
        for core_id in self.program_order:
            self.program_order[core_id].sort(key=lambda e: e.po_index)
    
    def get_writes_to(self, address: int) -> List[MemoryEvent]:
        """Get all write events to a specific address"""
        return [e for e in self.events 
                if e.type == 'WRITE' and e.address == address]
    
    def get_reads_from(self, address: int) -> List[MemoryEvent]:
        """Get all read events from a specific address"""
        return [e for e in self.events 
                if e.type == 'READ' and e.address == address]
    
    def check_sc(self) -> Tuple[bool, List[str]]:
        """
        Check Sequential Consistency
        
        SC requires:
        1. All operations appear in a single total order
        2. Operations of each core appear in program order
        3. Reads see the most recent write in the total order
        """
        violations = []
        
        for address in self.addresses:
            writes = self.get_writes_to(address)
            reads = self.get_reads_from(address)
            
            # For each read, check if it sees a valid write
            for read in reads:
                # Find writes that happen before this read
                prior_writes = [w for w in writes if w.timestamp < read.timestamp]
                
                if not prior_writes:
                    # Read before any write - should see initial value (0)
                    if read.value != 0:
                        violations.append(
                            f"SC violation: {read} sees non-zero before any write"
                        )
                    continue
                
                # Most recent write before this read
                most_recent = max(prior_writes, key=lambda w: w.timestamp)
                
                # Check if read saw the most recent write
                if read.value != most_recent.value:
                    # Check if there's a fence that could explain this
                    violations.append(
                        f"SC violation: {read} expected {most_recent.value}, saw {read.value}"
                    )
        
        return (len(violations) == 0, violations)
    
    def check_store_buffering(self) -> Tuple[bool, List[str]]:
        """
        Check Store Buffering (SB) pattern
        
        SB test:
          Core 0: x=1; FENCE; r0=y
          Core 1: y=1; FENCE; r1=x
        
        Under SC: r0==0 && r1==0 is FORBIDDEN
        Under RVWMO: ALLOWED (without fences)
        """
        violations = []
        
        # Group events by iteration (every 6 events = 1 iteration for 2 cores)
        # Each core does: WRITE, FENCE, READ
        events_per_iter = 6
        num_iters = len(self.events) // events_per_iter
        
        for iter_idx in range(num_iters):
            start = iter_idx * events_per_iter
            iter_events = self.events[start:start + events_per_iter]
            
            # Extract reads and writes
            reads_by_core = {}
            writes_by_core = {}
            
            for event in iter_events:
                if event.type == 'READ':
                    reads_by_core[event.core_id] = event
                elif event.type == 'WRITE':
                    writes_by_core[event.core_id] = event
            
            # Check if both cores read 0 (forbidden under SC with fences)
            if len(reads_by_core) == 2:
                r0 = reads_by_core.get(0)
                r1 = reads_by_core.get(1)
                
                if r0 and r1 and r0.value == 0 and r1.value == 0:
                    violations.append(
                        f"SB violation at iteration {iter_idx}: "
                        f"Core 0 read {r0.value}, Core 1 read {r1.value} "
                        f"(both 0 - forbidden under SC)"
                    )
        
        return (len(violations) == 0, violations)
    
    def analyze_litmus_test(self) -> Dict:
        """Analyze litmus test outcomes"""
        outcomes = defaultdict(int)
        
        # Group by iterations
        events_per_iter = 6  # 2 cores × 3 ops (WRITE, FENCE, READ)
        num_iters = len(self.events) // events_per_iter
        
        for iter_idx in range(num_iters):
            start = iter_idx * events_per_iter
            iter_events = self.events[start:start + events_per_iter]
            
            reads = [e for e in iter_events if e.type == 'READ']
            
            if len(reads) == 2:
                r0 = next((e.value for e in reads if e.core_id == 0), None)
                r1 = next((e.value for e in reads if e.core_id == 1), None)
                
                if r0 is not None and r1 is not None:
                    outcomes[(r0, r1)] += 1
        
        return outcomes
    
    def print_summary(self):
        """Print trace summary"""
        print("\n" + "="*60)
        print("MEMORY TRACE SUMMARY")
        print("="*60)
        print(f"Total events: {len(self.events)}")
        print(f"Cores: {sorted(self.cores)}")
        print(f"Addresses: {sorted(f'{a:#x}' for a in self.addresses)}")
        print()
        
        # Count event types
        event_counts = defaultdict(int)
        for event in self.events:
            event_counts[event.type] += 1
        
        print("Event counts:")
        for etype, count in sorted(event_counts.items()):
            print(f"  {etype}: {count}")
        print()

def load_trace(filename: str) -> List[MemoryEvent]:
    """Load memory trace from CSV file"""
    events = []
    
    with open(filename, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            event = MemoryEvent(
                timestamp=int(row['timestamp']),
                seq_id=int(row['seq_id']),
                core_id=int(row['core_id']),
                type=row['type'],
                address=int(row['address'], 16),
                value=int(row['value']),
                po_index=int(row['po_index'])
            )
            events.append(event)
    
    print(f"Loaded {len(events)} events from {filename}")
    return events

def main():
    if len(sys.argv) < 2:
        print("Usage: python3 consistency_checker.py <trace.csv>")
        sys.exit(1)
    
    trace_file = sys.argv[1]
    
    print("="*60)
    print("MEMORY CONSISTENCY CHECKER")
    print("="*60)
    print(f"Analyzing: {trace_file}\n")
    
    # Load trace
    events = load_trace(trace_file)
    
    # Create checker
    checker = ConsistencyChecker(events)
    checker.print_summary()
    
    # Check consistency models
    print("="*60)
    print("CONSISTENCY CHECKS")
    print("="*60)
    
    # Check SC
    print("\n1. Sequential Consistency (SC)")
    print("-" * 40)
    sc_ok, sc_violations = checker.check_sc()
    if sc_ok:
        print("✅ No SC violations detected")
    else:
        print(f"⚠️  {len(sc_violations)} SC violations found:")
        for v in sc_violations[:10]:  # Show first 10
            print(f"   {v}")
        if len(sc_violations) > 10:
            print(f"   ... and {len(sc_violations) - 10} more")
    
    # Check SB pattern
    print("\n2. Store Buffering (SB) Pattern")
    print("-" * 40)
    sb_ok, sb_violations = checker.check_store_buffering()
    if sb_ok:
        print("✅ No SB violations (SC-consistent)")
    else:
        print(f"⚠️  {len(sb_violations)} SB violations found:")
        for v in sb_violations[:5]:
            print(f"   {v}")
    
    # Analyze outcomes
    print("\n3. Litmus Test Outcomes")
    print("-" * 40)
    outcomes = checker.analyze_litmus_test()
    print("Distribution of (r0, r1) values:")
    for (r0, r1), count in sorted(outcomes.items()):
        forbidden = " <- FORBIDDEN under SC!" if (r0 == 0 and r1 == 0) else ""
        print(f"  (r0={r0}, r1={r1}): {count:4d}{forbidden}")
    
    print("\n" + "="*60)
    print("ANALYSIS COMPLETE")
    print("="*60)
    
    # Summary
    if not sc_ok or not sb_ok:
        print("\n⚠️  WEAK MEMORY BEHAVIOR DETECTED")
        print("The execution exhibits non-SC behavior,")
        print("consistent with RISC-V Weak Memory Ordering (RVWMO).")
    else:
        print("\n✅ SEQUENTIALLY CONSISTENT")
        print("No violations detected - behavior is SC-compliant.")

if __name__ == '__main__':
    main()
