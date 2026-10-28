#ifndef PAYLOCK_RECOVERY_MANAGER_H
#define PAYLOCK_RECOVERY_MANAGER_H

#include "deadlock/DeadlockDetector.h"
#include "locking/LockManager.h"
#include "transaction/TransactionManager.h"
#include "database/DatabaseManager.h"
#include <vector>
#include <string>
#include <mutex>
#include <set>
#include <chrono>

namespace paylock {

/**
 * @brief Policy for choosing the victim transaction to abort when resolving a deadlock cycle.
 */
enum class VictimPolicy {
    YOUNGEST_TRANSACTION,     // Abort most recently initiated transaction (highest txId)
    FEWEST_LOCKS_HELD,        // Abort transaction holding fewest resources
    LOWEST_COST_TO_ROLLBACK   // Abort transaction with smallest work footprint
};

std::string victimPolicyToString(VictimPolicy policy);

/**
 * @brief Represents the concrete outcome of a database rollback during deadlock recovery.
 */
enum class RollbackOutcome {
    SUCCESS,
    NO_TRANSACTION,        // Transaction had not opened an active DB transaction
    ALREADY_COMMITTED,     // Cannot rollback already committed transaction
    DATABASE_ERROR,        // Database error occurred during rollback
    SKIPPED                // Recovery not attempted or skipped
};

std::string rollbackOutcomeToString(RollbackOutcome outcome);

/**
 * @brief Structured report describing the execution and outcome of a deadlock recovery attempt.
 */
struct RecoveryResult {
    bool attempted{false};
    bool success{false};
    int victimTransactionId{-1};
    std::vector<int> deadlockCycle;
    RollbackOutcome rollbackOutcome{RollbackOutcome::SKIPPED};
    size_t locksReleased{0};
    size_t waitingTransactionsNotified{0};
    std::string failureReason;
    std::chrono::system_clock::time_point timestamp;

    std::string toString() const;
    void logResult() const;
};

/**
 * @brief Coordinates victim selection, safe cooperative abort, database rollback,
 * and lock release to restore concurrency following deadlock detection.
 * 
 * OS & DBMS Principles:
 * - Deadlock Recovery through selective transaction abortion.
 * - Strict non-destructive worker termination (workers unwind cooperatively).
 * - Full ACID rollback on MySQL connection to ensure balance integrity.
 * - Atomicity and lock-reclamation ensuring surviving transactions resume.
 */
class RecoveryManager {
public:
    explicit RecoveryManager(LockManager* lockManager = nullptr,
                             TransactionManager* txManager = nullptr,
                             DatabaseManager* dbManager = nullptr);

    void setLockManager(LockManager* lockManager) noexcept { lockManager_ = lockManager; }
    void setTransactionManager(TransactionManager* txManager) noexcept { txManager_ = txManager; }
    void setDatabaseManager(DatabaseManager* dbManager) noexcept { dbManager_ = dbManager; }

    LockManager* getLockManager() const noexcept { return lockManager_; }
    TransactionManager* getTransactionManager() const noexcept { return txManager_; }
    DatabaseManager* getDatabaseManager() const noexcept { return dbManager_; }

    // Victim Selection
    int selectVictim(const std::vector<int>& cyclePath, VictimPolicy policy = VictimPolicy::YOUNGEST_TRANSACTION);

    // Deadlock Recovery Execution (Step 6)
    RecoveryResult recover(const DeadlockReport& report, VictimPolicy policy = VictimPolicy::YOUNGEST_TRANSACTION);
    RecoveryResult recover(const std::vector<int>& cycle, VictimPolicy policy = VictimPolicy::YOUNGEST_TRANSACTION);

    // Backwards-compatible interface
    bool recoverDeadlock(int victimTxId, const std::string& reason = "Deadlock cycle resolution");

    size_t getRecoveryCount() const noexcept;
    void resetRecoveryCount() noexcept;
    std::vector<RecoveryResult> getRecoveryHistory() const;
    void recordRecoveryResult(const RecoveryResult& result);

    // Controlled Deadlock Recovery Demonstration Fixture (Step 6)
    static RecoveryResult demonstrateControlledDeadlockRecovery(LockManager& lm, DatabaseManager* db = nullptr);

private:
    LockManager* lockManager_{nullptr};
    TransactionManager* txManager_{nullptr};
    DatabaseManager* dbManager_{nullptr};

    mutable std::mutex recoveryMutex_;
    size_t totalRecoveries_{0};
    std::set<std::vector<int>> processedCycles_;
    std::vector<RecoveryResult> recoveryHistory_;
};

} // namespace paylock

#endif // PAYLOCK_RECOVERY_MANAGER_H
