/*
 * RISC-V Memory Consistency Tracer
 * Records memory operations for offline consistency checking
 */

#ifndef MC2LIB_TRACER_HPP_
#define MC2LIB_TRACER_HPP_

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <pthread.h>
#include <sys/time.h>

namespace mc2lib {
namespace tracer {

/**
 * @brief Event types for memory operations
 */
enum class EventType : uint8_t {
    READ = 0,
    WRITE = 1,
    FENCE = 2,
    ATOMIC_RMW = 3
};

/**
 * @brief Memory event record
 */
struct MemoryEvent {
    uint64_t timestamp;      // Nanosecond timestamp
    uint64_t seq_id;         // Sequential event ID (global counter)
    uint32_t core_id;        // Core/thread ID
    EventType type;          // Event type
    uint64_t address;        // Memory address
    uint32_t value;          // Value read/written
    uint32_t po_index;       // Program order index within thread
    
    void print() const {
        const char* type_str;
        switch (type) {
            case EventType::READ: type_str = "READ"; break;
            case EventType::WRITE: type_str = "WRITE"; break;
            case EventType::FENCE: type_str = "FENCE"; break;
            case EventType::ATOMIC_RMW: type_str = "ATOMIC"; break;
            default: type_str = "UNKNOWN";
        }
        
        printf("%lu,%lu,%u,%s,0x%lx,%u,%u\n",
               timestamp, seq_id, core_id, type_str, 
               address, value, po_index);
    }
};

/**
 * @brief Thread-local event buffer
 */
constexpr size_t MAX_EVENTS_PER_THREAD = 10000;

struct ThreadBuffer {
    MemoryEvent events[MAX_EVENTS_PER_THREAD];
    size_t count;
    uint32_t core_id;
    uint32_t po_counter;  // Program order counter
    
    ThreadBuffer() : count(0), core_id(0), po_counter(0) {}
};

/**
 * @brief Global tracer
 */
class Tracer {
private:
    static constexpr size_t MAX_THREADS = 64;
    
    ThreadBuffer thread_buffers[MAX_THREADS];
    std::atomic<uint64_t> global_seq_counter;
    std::atomic<uint32_t> next_core_id;
    pthread_key_t thread_key;
    
    static Tracer* instance_;
    
    Tracer() : global_seq_counter(0), next_core_id(0) {
        pthread_key_create(&thread_key, nullptr);
    }
    
public:
    static Tracer& getInstance() {
        if (!instance_) {
            instance_ = new Tracer();
        }
        return *instance_;
    }
    
    /**
     * @brief Register current thread and get core ID
     */
    uint32_t registerThread() {
        uint32_t core_id = next_core_id.fetch_add(1);
        
        ThreadBuffer* buf = &thread_buffers[core_id];
        buf->core_id = core_id;
        buf->count = 0;
        buf->po_counter = 0;
        
        pthread_setspecific(thread_key, buf);
        return core_id;
    }
    
    /**
     * @brief Get current thread's buffer
     */
    ThreadBuffer* getThreadBuffer() {
        return static_cast<ThreadBuffer*>(pthread_getspecific(thread_key));
    }
    
    /**
     * @brief Get high-resolution timestamp
     */
    static uint64_t getTimestamp() {
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return static_cast<uint64_t>(ts.tv_sec) * 1000000000ULL + ts.tv_nsec;
    }
    
    /**
     * @brief Record a memory event
     */
    void recordEvent(EventType type, uint64_t address, uint32_t value) {
        ThreadBuffer* buf = getThreadBuffer();
        if (!buf || buf->count >= MAX_EVENTS_PER_THREAD) {
            return;  // Buffer full or not registered
        }
        
        MemoryEvent& event = buf->events[buf->count++];
        event.timestamp = getTimestamp();
        event.seq_id = global_seq_counter.fetch_add(1);
        event.core_id = buf->core_id;
        event.type = type;
        event.address = address;
        event.value = value;
        event.po_index = buf->po_counter++;
    }
    
    /**
     * @brief Dump all recorded events to file
     */
    void dumpToFile(const char* filename) {
        FILE* f = fopen(filename, "w");
        if (!f) {
            perror("Failed to open trace file");
            return;
        }
        
        // CSV header
        fprintf(f, "timestamp,seq_id,core_id,type,address,value,po_index\n");
        
        // Collect all events from all threads
        for (size_t tid = 0; tid < next_core_id.load(); tid++) {
            ThreadBuffer& buf = thread_buffers[tid];
            for (size_t i = 0; i < buf.count; i++) {
                const MemoryEvent& event = buf.events[i];
                fprintf(f, "%lu,%lu,%u,", 
                        event.timestamp, event.seq_id, event.core_id);
                
                switch (event.type) {
                    case EventType::READ: fprintf(f, "READ"); break;
                    case EventType::WRITE: fprintf(f, "WRITE"); break;
                    case EventType::FENCE: fprintf(f, "FENCE"); break;
                    case EventType::ATOMIC_RMW: fprintf(f, "ATOMIC"); break;
                }
                
                fprintf(f, ",0x%lx,%u,%u\n",
                        event.address, event.value, event.po_index);
            }
        }
        
        fclose(f);
        printf("Trace dumped to: %s\n", filename);
        printf("Total events: %lu\n", global_seq_counter.load());
    }
    
    /**
     * @brief Print statistics
     */
    void printStats() {
        printf("\n=== Tracer Statistics ===\n");
        printf("Total threads: %u\n", next_core_id.load());
        printf("Total events: %lu\n", global_seq_counter.load());
        
        for (size_t tid = 0; tid < next_core_id.load(); tid++) {
            ThreadBuffer& buf = thread_buffers[tid];
            printf("  Core %u: %zu events\n", buf.core_id, buf.count);
        }
        printf("=========================\n");
    }
};

// Static instance
Tracer* Tracer::instance_ = nullptr;

/**
 * @brief Instrumented memory operations
 */
class InstrumentedMemory {
private:
    volatile uint8_t* memory_;
    size_t size_;
    
public:
    InstrumentedMemory(size_t size) : size_(size) {
        memory_ = new volatile uint8_t[size];
        memset((void*)memory_, 0, size);
    }
    
    ~InstrumentedMemory() {
        delete[] memory_;
    }
    
    /**
     * @brief Instrumented read (records event)
     */
    uint32_t read(uint64_t addr) {
        uint32_t value = memory_[addr];
        Tracer::getInstance().recordEvent(EventType::READ, addr, value);
        return value;
    }
    
    /**
     * @brief Instrumented write (records event)
     */
    void write(uint64_t addr, uint32_t value) {
        Tracer::getInstance().recordEvent(EventType::WRITE, addr, value);
        memory_[addr] = static_cast<uint8_t>(value);
    }
    
    /**
     * @brief Instrumented fence (records event)
     */
    void fence() {
        Tracer::getInstance().recordEvent(EventType::FENCE, 0, 0);
        __sync_synchronize();
    }
    
    /**
     * @brief Instrumented atomic RMW
     */
    uint32_t atomic_swap(uint64_t addr, uint32_t new_value) {
        // Record as atomic operation
        uint32_t old_value = __sync_lock_test_and_set((volatile uint8_t*)&memory_[addr], 
                                                       (uint8_t)new_value);
        Tracer::getInstance().recordEvent(EventType::ATOMIC_RMW, addr, old_value);
        return old_value;
    }
    
    /**
     * @brief Get base address (for code generation)
     */
    volatile uint8_t* getBaseAddress() {
        return memory_;
    }
};

} // namespace tracer
} // namespace mc2lib

/**
 * @brief Helper macros for instrumentation
 */
#define TRACER_INIT() mc2lib::tracer::Tracer::getInstance()
#define TRACER_REGISTER() mc2lib::tracer::Tracer::getInstance().registerThread()
#define TRACER_DUMP(filename) mc2lib::tracer::Tracer::getInstance().dumpToFile(filename)
#define TRACER_STATS() mc2lib::tracer::Tracer::getInstance().printStats()

#endif // MC2LIB_TRACER_HPP_

/* vim: set ts=4 sts=4 sw=4 et : */
