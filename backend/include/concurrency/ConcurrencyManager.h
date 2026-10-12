#ifndef PAYLOCK_CONCURRENCY_MANAGER_H
#define PAYLOCK_CONCURRENCY_MANAGER_H

#include "database/DatabaseManager.h"
#include "transaction/TransactionManager.h"
#include "concurrency/ConcurrentTransactionProcessor.h"
#include <string>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <unordered_map>
#include <functional>
#include <chrono>

namespace paylock {

/**
 * @brief Transaction Job Type submitted to the worker pool.
 */
enum class TransactionJobType {
    TRANSFER,
    DEPOSIT,
    WITHDRAW
};

/**
 * @brief Encapsulates a transaction job for asynchronous concurrent execution.
 */
struct TransactionJob {
    std::string jobId; // e.g. "T001"
    TransactionJobType type{TransactionJobType::TRANSFER};
    int senderAccount{0};
    int receiverAccount{0};
    double amount{0.0};
    std::function<void(bool success, const std::string& error)> onComplete{nullptr};
};

/**
 * @brief Information about an active or completed worker thread.
 * 
 * OS Concept: Thread identification and lifecycle state tracking.
 */
struct WorkerInfo {
    std::string workerId; // e.g. "W001"
    ThreadState state{ThreadState::CREATED};
    std::string currentTask; // e.g. "T001: A101 -> A102 (₹2000)"
    int completedCount{0};
};

/**
 * @brief Real-time thread-safe concurrency metrics.
 */
struct ConcurrencySystemStats {
    int totalTransactions{0};
    int completedTransactions{0};
    int failedTransactions{0};
    int activeThreads{0};
    int peakConcurrentThreads{0};
};

/**
 * @brief Results for the isolated Shared Counter Critical Section & Race Condition demo.
 */
struct CounterDemoResult {
    int numThreads{0};
    int incrementsPerThread{0};
    int expectedCount{0};
    int unsafeFinalCount{0};
    int safeFinalCount{0};
    bool raceConditionOccurred{false};
};

/**
 * @brief ConcurrencyManager orchestrates worker threads, task dispatching,
 * thread state lifecycle management, synchronization, and race-condition prevention.
 * 
 * OS Concepts Demonstrated:
 * - Thread Management (`std::thread`, thread creation, join, lifecycle control)
 * - Worker Thread Pool with non-busy waiting (`std::condition_variable`)
 * - Mutual Exclusion (`std::mutex`, `std::lock_guard`, `std::unique_lock`)
 * - Critical Sections (Application-level synchronized balance updates)
 * - Thread State Lifecycle Tracking (`CREATED` -> `WAITING` -> `RUNNING` -> `COMPLETED`/`FAILED`)
 * - Atomic Variables (`std::atomic` for lock-free concurrency statistics)
 * - Controlled Race Condition & Critical Section Demonstrations
 * 
 * DBMS Concepts Integrated:
 * - Thread-isolated MySQL connections (each worker owns an independent connection).
 * - Multi-threaded invocation of TransactionManager ACID transactions.
 */
class ConcurrencyManager {
public:
    explicit ConcurrencyManager(DatabaseConfig config = DatabaseConfig::fromEnvOrDefault());
    ~ConcurrencyManager();

    // Non-copyable, non-movable due to thread management and mutexes
    ConcurrencyManager(const ConcurrencyManager&) = delete;
    ConcurrencyManager& operator=(const ConcurrencyManager&) = delete;

    // Worker Pool Lifecycle
    void start(size_t numWorkers = 4);
    void submitTransaction(const TransactionJob& job);
    void submitTransfer(const std::string& jobId, int sender, int receiver, double amount);
    void submitDeposit(const std::string& jobId, int account, double amount);
    void submitWithdraw(const std::string& jobId, int account, double amount);
    void waitForCompletion();
    void shutdown();
    bool isRunning() const noexcept;

    // Direct Batch Operations (High-level wrappers)
    ConcurrencyMetrics executeBatch(const std::vector<TransferRequest>& transfers);
    ConcurrencyMetrics executeWithdrawals(const std::vector<WithdrawalRequest>& withdrawals);

    // Thread State Queries (Thread-Safe)
    WorkerInfo getWorkerInfo(const std::string& workerId) const;
    std::vector<WorkerInfo> getAllWorkers() const;

    // Concurrency Statistics (Thread-Safe)
    ConcurrencySystemStats getSystemStats() const;
    void resetStats();

    // Controlled Demonstrations (OS Textbook Concepts)
    static CounterDemoResult demonstrateCounterRaceCondition(int numThreads = 10, int incrementsPerThread = 10000);
    void runRaceConditionDemo(double balance = 10000.0, double w1 = 7000.0, double w2 = 6000.0);

    // Access to underlying processor
    ConcurrentTransactionProcessor& getProcessor() noexcept;

private:
    void workerLoop(const std::string& workerId);
    void updateWorkerState(const std::string& workerId, ThreadState state, const std::string& task = "");

    DatabaseConfig dbConfig_;
    ConcurrentTransactionProcessor processor_;

    // Worker Threads & Synchronization
    std::vector<std::thread> workers_;
    std::queue<TransactionJob> jobQueue_;
    mutable std::mutex queueMutex_;
    std::condition_variable taskCv_;
    std::condition_variable completionCv_;
    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> isRunning_{false};

    // Shared State & Thread Tracking
    mutable std::mutex registryMutex_;
    std::unordered_map<std::string, WorkerInfo> workerRegistry_;

    // Application Critical Section Mutex for balance serialization
    std::mutex criticalSectionMutex_;

    // Atomic Concurrency Metrics
    std::atomic<int> statTotalJobs_{0};
    std::atomic<int> statCompletedJobs_{0};
    std::atomic<int> statFailedJobs_{0};
    std::atomic<int> statActiveThreads_{0};
    std::atomic<int> statPeakThreads_{0};
};

} // namespace paylock

#endif // PAYLOCK_CONCURRENCY_MANAGER_H
