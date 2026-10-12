#ifndef PAYLOCK_CONCURRENCY_MANAGER_H
#define PAYLOCK_CONCURRENCY_MANAGER_H

#include "concurrency/ConcurrentTransactionProcessor.h"
#include <vector>
#include <memory>

namespace paylock {

/**
 * @brief High-level Concurrency Manager orchestrating thread allocation,
 * synchronization, and transaction worker threads.
 * 
 * OS Concepts Demonstrated:
 * - Thread Management (`std::thread`)
 * - Critical Sections & Mutex Protection (`std::mutex`, `std::lock_guard`)
 * - Foundation for future Thread Pool & Condition Variable signaling.
 */
class ConcurrencyManager {
public:
    explicit ConcurrencyManager(DatabaseConfig config = DatabaseConfig::fromEnvOrDefault());

    // Batch concurrent execution
    ConcurrencyMetrics executeBatch(const std::vector<TransferRequest>& transfers);
    ConcurrencyMetrics executeWithdrawals(const std::vector<WithdrawalRequest>& withdrawals);

    // Race condition simulation
    void runRaceConditionDemo(double balance = 10000.0, double w1 = 7000.0, double w2 = 6000.0);

    // Underlying processor access
    ConcurrentTransactionProcessor& getProcessor() noexcept;

private:
    ConcurrentTransactionProcessor processor_;
};

} // namespace paylock

#endif // PAYLOCK_CONCURRENCY_MANAGER_H
