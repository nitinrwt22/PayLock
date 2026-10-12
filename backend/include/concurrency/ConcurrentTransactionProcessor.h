#ifndef PAYLOCK_CONCURRENT_TRANSACTION_PROCESSOR_H
#define PAYLOCK_CONCURRENT_TRANSACTION_PROCESSOR_H

#include "database/DatabaseManager.h"
#include "transaction/TransactionManager.h"
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <chrono>
#include <atomic>

namespace paylock {

/**
 * @brief Thread lifecycle state enumeration.
 * 
 * OS Concept: Thread States & Lifecycle Transitions.
 */
enum class ThreadState {
    CREATED,
    WAITING,
    WAITING_FOR_MUTEX,
    RUNNING,
    COMPLETED,
    FAILED
};

std::string threadStateToString(ThreadState state);

/**
 * @brief Encapsulates a concurrent fund transfer request.
 */
struct TransferRequest {
    std::string threadLabel; // e.g. "T1"
    int senderAccount;
    int receiverAccount;
    double amount;
};

/**
 * @brief Encapsulates a concurrent withdrawal request.
 */
struct WithdrawalRequest {
    std::string threadLabel; // e.g. "T1"
    int accountId;
    double amount;
};

/**
 * @brief Performance and execution metrics for a concurrent batch test.
 */
struct ConcurrencyMetrics {
    int totalThreads{0};
    int successfulTransactions{0};
    int failedTransactions{0};
    long long executionTimeMs{0};

    void print() const;
};

/**
 * @brief ConcurrentTransactionProcessor coordinates multithreaded transaction
 * execution, mutual exclusion, critical section protection, and thread-safe logging.
 * 
 * OS Concepts Demonstrated:
 * - Multithreading (`std::thread`)
 * - Mutual Exclusion (`std::mutex`, `std::lock_guard`)
 * - Critical Sections (Application-level synchronized blocks)
 * - Race Condition Demonstration (Unsynchronized vs. Synchronized access)
 * - Thread State Lifecycle Tracking (`CREATED` -> `WAITING` -> `RUNNING` -> `COMPLETED`/`FAILED`)
 * - Thread-Safe Console Logging
 * 
 * DBMS Concepts Integrated:
 * - Per-thread isolated database connections preventing socket contention.
 * - Integration with TransactionManager & MySQL ACID transactions (`START TRANSACTION`, `COMMIT`, `ROLLBACK`).
 */
class ConcurrentTransactionProcessor {
public:
    explicit ConcurrentTransactionProcessor(DatabaseConfig config = DatabaseConfig::fromEnvOrDefault());

    // ------------------------------------------------------------------------
    // Real Payment Concurrency Operations
    // ------------------------------------------------------------------------

    /**
     * @brief Executes multiple transfer requests concurrently using std::thread.
     * All worker threads are launched before any thread is joined.
     */
    ConcurrencyMetrics processConcurrentTransfers(const std::vector<TransferRequest>& requests);

    /**
     * @brief Executes multiple withdrawal requests concurrently on target accounts.
     */
    ConcurrencyMetrics processConcurrentWithdrawals(const std::vector<WithdrawalRequest>& requests);

    // ------------------------------------------------------------------------
    // Controlled Race Condition Demonstrations (In-Memory Safe)
    // ------------------------------------------------------------------------

    /**
     * @brief Demonstrates the classic Race Condition and Critical Section problem.
     * Compares unsynchronized concurrent access vs. mutex-protected access on a shared balance.
     * Does NOT corrupt the production database.
     */
    static void demonstrateRaceCondition(double initialBalance = 10000.00,
                                         double withdrawT1 = 7000.00,
                                         double withdrawT2 = 6000.00);

    // ------------------------------------------------------------------------
    // Thread-Safe Logging Helpers
    // ------------------------------------------------------------------------
    static void log(const std::string& threadLabel, const std::string& message);
    static void logState(const std::string& threadLabel, ThreadState state, const std::string& detail = "");

private:
    DatabaseConfig dbConfig_;

    // Mutex protecting the critical section for shared account updates
    std::mutex paymentMutex_;

    // Mutex ensuring atomic, un-interleaved terminal log messages
    static std::mutex consoleMutex_;
};

} // namespace paylock

#endif // PAYLOCK_CONCURRENT_TRANSACTION_PROCESSOR_H
