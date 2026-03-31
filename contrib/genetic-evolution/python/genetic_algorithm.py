"""
Genetic Algorithm - 遗传算法实现

实现真正的选择、交叉、变异操作
基于 McVerSi 论文的遗传算法策略
"""

import random
import logging
from typing import List, Set, Tuple, Dict, Any
from dataclasses import dataclass, field
from copy import deepcopy

logger = logging.getLogger(__name__)


@dataclass
class TestGenome:
    """测试基因组"""
    test_id: str
    generation: int
    parent_ids: List[str] = field(default_factory=list)
    
    # 测试参数
    num_threads: int = 2
    num_iterations: int = 10
    
    # 操作序列（每个线程的操作）
    # operations[thread_id] = [(op_type, address, value), ...]
    operations: Dict[int, List[Tuple[str, int, int]]] = field(default_factory=dict)
    
    # 适应度相关
    fitness: float = 0.0
    coverage: Dict[str, Any] = field(default_factory=dict)
    
    def __post_init__(self):
        if not self.operations:
            self.operations = {0: [], 1: []}
        if not self.coverage:
            self.coverage = {
                'address_pairs': [],
                'new_pairs': [],
                'violations': []
            }


class GeneticAlgorithm:
    """遗传算法引擎"""
    
    def __init__(self, config: Dict):
        self.population_size = config.get('population_size', 50)
        self.elite_ratio = config.get('elite_ratio', 0.1)
        self.mutation_rate = config.get('mutation_rate', 0.1)
        self.p_usel = config.get('P_USEL', 0.2)  # 无条件选择概率
        self.p_bfa = config.get('P_BFA', 0.05)   # 从 fitaddr 构建概率
        
        self.min_threads = config.get('min_threads', 2)
        self.max_threads = config.get('max_threads', 2)
        self.min_ops = config.get('min_ops_per_thread', 10)
        self.max_ops = config.get('max_ops_per_thread', 30)
        
        # 可用的内存地址（64 字节对齐）
        self.available_addresses = [i * 64 for i in range(16)]  # 0, 64, 128, ..., 960
        
        # 操作类型
        self.op_types = ['WRITE', 'READ', 'FENCE']
        
        logger.info(f"GeneticAlgorithm initialized:")
        logger.info(f"  Population: {self.population_size}")
        logger.info(f"  Elite ratio: {self.elite_ratio}")
        logger.info(f"  Mutation rate: {self.mutation_rate}")
        logger.info(f"  P_USEL: {self.p_usel}, P_BFA: {self.p_bfa}")
    
    def generate_initial_population(self, seed: int = None) -> List[TestGenome]:
        """生成初始种群"""
        if seed is not None:
            random.seed(seed)
        
        logger.info(f"Generating initial population of {self.population_size} tests")
        
        population = []
        for i in range(self.population_size):
            genome = self._create_random_genome(f"gen0_test_{i}", 0)
            population.append(genome)
        
        logger.info(f"Generated {len(population)} initial tests")
        return population
    
    def _create_random_genome(self, test_id: str, generation: int, 
                             fitaddrs: Set[Tuple[int, int]] = None) -> TestGenome:
        """创建随机基因组"""
        genome = TestGenome(
            test_id=test_id,
            generation=generation,
            num_threads=self.min_threads
        )
        
        # 为每个线程生成操作序列
        for thread_id in range(genome.num_threads):
            num_ops = random.randint(self.min_ops, self.max_ops)
            ops = []
            
            for _ in range(num_ops):
                # 选择地址
                if fitaddrs and random.random() < self.p_bfa:
                    # 从 fitaddr 选择
                    addr_pair = random.choice(list(fitaddrs))
                    address = random.choice(addr_pair)
                else:
                    # 随机选择
                    address = random.choice(self.available_addresses)
                
                # 选择操作类型
                op_type = random.choice(self.op_types)
                
                # 值（只对 WRITE 有意义）
                value = 1 if op_type == 'WRITE' else 0
                
                ops.append((op_type, address, value))
            
            genome.operations[thread_id] = ops
        
        return genome
    
    def evolve(self, population: List[TestGenome], 
               fitaddrs: Set[Tuple[int, int]]) -> List[TestGenome]:
        """演化种群（一代）"""
        logger.info(f"Evolving population (size={len(population)})")
        
        # 1. 按适应度排序
        sorted_pop = sorted(population, key=lambda g: g.fitness, reverse=True)
        
        # 2. 精英保留
        elite_count = max(1, int(len(population) * self.elite_ratio))
        elites = sorted_pop[:elite_count]
        logger.info(f"  Keeping {elite_count} elites (best fitness: {elites[0].fitness:.4f})")
        
        # 3. 生成新个体
        offspring = []
        generation = population[0].generation + 1
        offspring_count = len(population) - elite_count
        
        for i in range(offspring_count):
            # 选择父代
            parent1 = self._select_parent(sorted_pop)
            parent2 = self._select_parent(sorted_pop)
            
            # 交叉
            child = self._crossover(parent1, parent2, generation, i, fitaddrs)
            
            # 变异
            if random.random() < self.mutation_rate:
                child = self._mutate(child, fitaddrs)
            
            offspring.append(child)
        
        logger.info(f"  Generated {len(offspring)} offspring")
        
        # 4. 返回新种群
        new_population = elites + offspring
        return new_population
    
    def _select_parent(self, sorted_population: List[TestGenome]) -> TestGenome:
        """选择父代（锦标赛选择 + 无条件选择）"""
        # McVerSi 策略：P_USEL 概率无条件选择任意个体
        if random.random() < self.p_usel:
            return random.choice(sorted_population)
        
        # 锦标赛选择（tournament size = 3）
        tournament_size = 3
        tournament = random.sample(sorted_population, 
                                  min(tournament_size, len(sorted_population)))
        return max(tournament, key=lambda g: g.fitness)
    
    def _crossover(self, parent1: TestGenome, parent2: TestGenome,
                  generation: int, index: int,
                  fitaddrs: Set[Tuple[int, int]]) -> TestGenome:
        """交叉操作（混合父代的操作序列）"""
        child = TestGenome(
            test_id=f"gen{generation}_test_{index}",
            generation=generation,
            parent_ids=[parent1.test_id, parent2.test_id],
            num_threads=parent1.num_threads
        )
        
        # 对每个线程，混合父代的操作
        for thread_id in range(child.num_threads):
            ops1 = parent1.operations.get(thread_id, [])
            ops2 = parent2.operations.get(thread_id, [])
            
            # 单点交叉
            if ops1 and ops2:
                # 随机选择交叉点
                max_len = max(len(ops1), len(ops2))
                crossover_point = random.randint(1, max_len - 1) if max_len > 1 else 0
                
                # 混合
                child_ops = ops1[:crossover_point] + ops2[crossover_point:]
            elif ops1:
                child_ops = ops1.copy()
            elif ops2:
                child_ops = ops2.copy()
            else:
                child_ops = []
            
            # 限制长度
            if len(child_ops) > self.max_ops:
                child_ops = child_ops[:self.max_ops]
            
            child.operations[thread_id] = child_ops
        
        return child
    
    def _mutate(self, genome: TestGenome, 
               fitaddrs: Set[Tuple[int, int]]) -> TestGenome:
        """变异操作"""
        # 对每个线程的操作序列进行变异
        for thread_id in range(genome.num_threads):
            ops = genome.operations.get(thread_id, [])
            
            if not ops:
                continue
            
            # 选择变异类型
            mutation_type = random.choice(['change_address', 'change_op', 'swap', 'insert', 'delete'])
            
            if mutation_type == 'change_address' and ops:
                # 改变一个操作的地址
                idx = random.randint(0, len(ops) - 1)
                op_type, old_addr, value = ops[idx]
                
                # 从 fitaddr 选择新地址（如果有）
                if fitaddrs and random.random() < self.p_bfa:
                    addr_pair = random.choice(list(fitaddrs))
                    new_addr = random.choice(addr_pair)
                else:
                    new_addr = random.choice(self.available_addresses)
                
                ops[idx] = (op_type, new_addr, value)
            
            elif mutation_type == 'change_op' and ops:
                # 改变一个操作的类型
                idx = random.randint(0, len(ops) - 1)
                _, address, _ = ops[idx]
                new_op_type = random.choice(self.op_types)
                new_value = 1 if new_op_type == 'WRITE' else 0
                ops[idx] = (new_op_type, address, new_value)
            
            elif mutation_type == 'swap' and len(ops) >= 2:
                # 交换两个操作的顺序
                idx1 = random.randint(0, len(ops) - 1)
                idx2 = random.randint(0, len(ops) - 1)
                ops[idx1], ops[idx2] = ops[idx2], ops[idx1]
            
            elif mutation_type == 'insert' and len(ops) < self.max_ops:
                # 插入新操作
                idx = random.randint(0, len(ops))
                
                if fitaddrs and random.random() < self.p_bfa:
                    addr_pair = random.choice(list(fitaddrs))
                    address = random.choice(addr_pair)
                else:
                    address = random.choice(self.available_addresses)
                
                op_type = random.choice(self.op_types)
                value = 1 if op_type == 'WRITE' else 0
                ops.insert(idx, (op_type, address, value))
            
            elif mutation_type == 'delete' and len(ops) > self.min_ops:
                # 删除一个操作
                idx = random.randint(0, len(ops) - 1)
                del ops[idx]
            
            genome.operations[thread_id] = ops
        
        return genome
    
    def genome_to_c_code(self, genome: TestGenome) -> str:
        """将基因组转换为 C 代码"""
        code = f"""/*
 * Generated test: {genome.test_id}
 * Generation: {genome.generation}
 * Parents: {', '.join(genome.parent_ids) if genome.parent_ids else 'None'}
 */

#define _POSIX_C_SOURCE 199309L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Event tracer
#define MAX_EVENTS 10000

typedef enum {{
    EVENT_READ = 0,
    EVENT_WRITE = 1,
    EVENT_FENCE = 2
}} EventType;

typedef struct {{
    unsigned long timestamp;
    unsigned long seq_id;
    unsigned int core_id;
    EventType type;
    unsigned long address;
    unsigned int value;
    unsigned int po_index;
}} MemoryEvent;

static MemoryEvent events[MAX_EVENTS];
static unsigned int event_count = 0;
static unsigned int po_counter = 0;
static unsigned int core_id = 0;

static unsigned long get_timestamp() {{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}}

static void record_event(EventType type, unsigned long addr, unsigned int value) {{
    if (event_count >= MAX_EVENTS) return;
    MemoryEvent* e = &events[event_count++];
    e->timestamp = get_timestamp();
    e->seq_id = event_count - 1;
    e->core_id = core_id;
    e->type = type;
    e->address = addr;
    e->value = value;
    e->po_index = po_counter++;
}}

static volatile unsigned char test_mem[1024] __attribute__((aligned(64)));

static unsigned int inst_read(unsigned long addr) {{
    unsigned int value = test_mem[addr];
    record_event(EVENT_READ, addr, value);
    return value;
}}

static void inst_write(unsigned long addr, unsigned int value) {{
    record_event(EVENT_WRITE, addr, value);
    test_mem[addr] = (unsigned char)value;
}}

static void inst_fence() {{
    record_event(EVENT_FENCE, 0, 0);
    __sync_synchronize();
}}

static void run_test() {{
"""
        
        # 生成每个线程的代码
        for thread_id in range(genome.num_threads):
            ops = genome.operations.get(thread_id, [])
            
            if thread_id == 0:
                code += f"    if (core_id == {thread_id}) {{\n"
            else:
                code += f"    }} else if (core_id == {thread_id}) {{\n"
            
            # 生成操作序列
            for op_type, address, value in ops:
                if op_type == 'WRITE':
                    code += f"        inst_write({address}, {value});\n"
                elif op_type == 'READ':
                    code += f"        inst_read({address});\n"
                elif op_type == 'FENCE':
                    code += f"        inst_fence();\n"
        
        code += "    }\n"
        code += "}\n\n"
        
        # dump 函数
        code += """static void dump_trace() {
    char filename[256];
    // 输出到测试运行目录
    const char* run_dir = getenv("TEST_RUN_DIR");
    if (run_dir) {
        snprintf(filename, sizeof(filename), "%s/memory_trace_core%u.csv", run_dir, core_id);
    } else {
        snprintf(filename, sizeof(filename), "memory_trace_core%u.csv", core_id);
    }
    FILE* f = fopen(filename, "w");
    if (!f) return;
    fprintf(f, "timestamp,seq_id,core_id,type,address,value,po_index\\n");
    for (unsigned int i = 0; i < event_count && i < MAX_EVENTS; ++i) {
        MemoryEvent* e = &events[i];
        const char* type_str;
        switch (e->type) {
            case EVENT_READ: type_str = "READ"; break;
            case EVENT_WRITE: type_str = "WRITE"; break;
            case EVENT_FENCE: type_str = "FENCE"; break;
            default: type_str = "UNKNOWN";
        }
        fprintf(f, "%lu,%lu,%u,%s,0x%lx,%u,%u\\n",
                e->timestamp, e->seq_id, e->core_id, type_str,
                e->address, e->value, e->po_index);
    }
    fclose(f);
}

"""
        
        # main
        code += f"""int main(int argc, char* argv[]) {{
    if (argc > 1) core_id = atoi(argv[1]);
    if (core_id == 0) memset((void*)test_mem, 0, sizeof(test_mem));
    
    // Small startup delay
    for (volatile long i = 0; i < (core_id + 1) * 1000000; i++);
    
    // Run {genome.num_iterations} iterations
    for (int iter = 0; iter < {genome.num_iterations}; iter++) {{
        // Reset (only core 0)
        if (core_id == 0) {{
            memset((void*)test_mem, 0, sizeof(test_mem));
            __sync_synchronize();
        }} else {{
            for (volatile int j = 0; j < 1000; j++);
        }}
        
        run_test();
        
        // Wait between iterations
        for (volatile int j = 0; j < 100000; j++);
    }}
    
    dump_trace();
    return 0;
}}
"""
        
        return code


def main():
    """测试遗传算法"""
    logging.basicConfig(level=logging.INFO)
    
    config = {
        'population_size': 10,
        'elite_ratio': 0.1,
        'mutation_rate': 0.2,
        'P_USEL': 0.2,
        'P_BFA': 0.05,
        'min_ops_per_thread': 5,
        'max_ops_per_thread': 10
    }
    
    ga = GeneticAlgorithm(config)
    
    # 生成初始种群
    population = ga.generate_initial_population(seed=42)
    
    print("\n" + "=" * 60)
    print("Initial Population:")
    print("=" * 60)
    for i, genome in enumerate(population[:3]):
        print(f"\nTest {i}: {genome.test_id}")
        print(f"  Thread 0 ops: {len(genome.operations[0])}")
        print(f"  Thread 1 ops: {len(genome.operations[1])}")
        print(f"  Sample ops (thread 0): {genome.operations[0][:3]}")
    
    # 模拟适应度
    for genome in population:
        genome.fitness = random.random()
    
    # 演化
    fitaddrs = {(0, 64), (64, 128), (128, 192)}
    new_population = ga.evolve(population, fitaddrs)
    
    print("\n" + "=" * 60)
    print("After Evolution:")
    print("=" * 60)
    for i, genome in enumerate(new_population[:3]):
        print(f"\nTest {i}: {genome.test_id}")
        print(f"  Generation: {genome.generation}")
        print(f"  Parents: {genome.parent_ids}")
        print(f"  Thread 0 ops: {len(genome.operations[0])}")
        print(f"  Thread 1 ops: {len(genome.operations[1])}")
    
    # 生成 C 代码
    print("\n" + "=" * 60)
    print("Sample C Code:")
    print("=" * 60)
    code = ga.genome_to_c_code(new_population[0])
    print(code[:500] + "\n... (truncated)")


if __name__ == '__main__':
    main()
