#include "recovery/RecoveryManager.h"
#include "logging/Logger.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <cassert>

namespace paylock {

std::string victimPolicyToString(VictimPolicy policy) {
    switch (policy) {
        case VictimPolicy::YOUNGEST_TRANSACTION:     return "YOUNGEST_TRANSACTION";
        case VictimPolicy::FEWEST_LOCKS_HELD:        return "FEWEST_LOCKS_HELD";
        case VictimPolicy::LOWEST_COST_TO_ROLLBACK:  return "LOWEST_COST_TO_ROLLBACK";
    }
    return "UNKNOWN";
}

std::string rollbackOutcomeToString(RollbackOutcome outcome) {
    switch (outcome) {
        case RollbackOutcome::SUCCESS:           return "SUCCESS";
        case RollbackOutcome::NO_TRANSACTION:    return "NO_TRANSACTION";
        case RollbackOutcome::ALREADY_COMMITTED: return "ALREADY_COMMITTED";
        case RollbackOutcome::DATABASE_ERROR:    return "DATABASE_ERROR";
        case RollbackOutcome::SKIPPED:           return "SKIPPED";
    }
    return "UNKNOWN";
}

std::string RecoveryResult::toString() const {
    std::ostringstream ss;
    ss << "=================================================================\n";
    ss << "                    DEADLOCK RECOVERY REPORT                     \n";
    ss << "=================================================================\n";
    ss << "Status: " << (success ? "RECOVERY SUCCESSFUL" : (attempted ? "RECOVERY FAILED" : "RECOVERY SKIPPED")) << "\n";
    ss << "Victim Transaction: " << (victimTransactionId > 0 ? ("T" + std::to_string(victimTransactionId)) : "None") << "\n";
    if (!deadlockCycle.empty()) {
        ss << "Resolved Cycle: " << DeadlockDetector::formatCycle(deadlockCycle) << "\n";
    }
    ss << "Rollback Outcome: " << rollbackOutcomeToString(rollbackOutcome) << "\n";
    ss << "Locks Released: " << locksReleased << "\n";
    ss << "Waiting Transactions Notified: " << waitingTransactionsNotified << "\n";
    if (!failureReason.empty()) {
        ss << "Details / Failure: " << failureReason << "\n";
    }
    ss << "=================================================================\n";
    return ss.str();
}

void RecoveryResult::logResult() const {
    if (success) {
        Logger::info("Deadlock recovery completed successfully for victim T" + 
                     std::to_string(victimTransactionId), "RecoveryManager");
    } else if (attempted) {
        Logger::warn("Deadlock recovery attempt failed: " + failureReason, "RecoveryManager");
    }
}

// =========================================================================
// RecoveryManager Implementation
// =========================================================================

RecoveryManager::RecoveryManager(LockManager* lockManager,
                                 TransactionManager* txManager,
                                 DatabaseManager* dbManager)
    : lockManager_(lockManager), txManager_(txManager), dbManager_(dbManager) {}

int RecoveryManager::selectVictim(const std::vector<int>& cyclePath, VictimPolicy policy) {
    std::lock_guard<std::mutex> lock(recoveryMutex_);
    if (cyclePath.empty()) {
        return -1;
    }

    // Extract unique transactions participating in the cycle
    std::vector<int> uniqueNodes;
    for (int node : cyclePath) {
        if (std::find(uniqueNodes.begin(), uniqueNodes.end(), node) == uniqueNodes.end()) {
            uniqueNodes.push_back(node);
        }
    }

    if (uniqueNodes.empty()) {
        return -1;
    }

    int victim = -1;

    if (policy == VictimPolicy::FEWEST_LOCKS_HELD && lockManager_ != nullptr) {
        // Select member holding the fewest locks; tie-break with highest txId
        size_t minLocks = std::numeric_limits<size_t>::max();
        for (int tx : uniqueNodes) {
            size_t locks = lockManager_->getLocksForTransaction(tx).size();
            if (locks < minLocks || (locks == minLocks && tx > victim)) {
                minLocks = locks;
                victim = tx;
            }
        }
    } else {
        // Default policy (YOUNGEST_TRANSACTION & LOWEST_COST): highest transaction ID
        victim = *std::max_element(uniqueNodes.begin(), uniqueNodes.end());
    }

    Logger::info("Selected victim: T" + std::to_string(victim) + " (Policy: " + victimPolicyToString(policy) + ")", "RecoveryManager");
    return victim;
}

RecoveryResult RecoveryManager::recover(const DeadlockReport& report, VictimPolicy policy) {
    std::lock_guard<std::mutex> lock(recoveryMutex_);
    RecoveryResult result;
    result.timestamp = std::chrono::system_clock::now();

    if (!report.deadlockDetected || report.cycles.empty()) {
        result.attempted = false;
        result.success = false;
        result.rollbackOutcome = RollbackOutcome::SKIPPED;
        result.failureReason = "No deadlock cycle detected in report.";
        return result;
    }

    result.attempted = true;
    const auto& cycle = report.cycles.front();
    result.deadlockCycle = cycle;

    // Idempotency: verify this exact cycle has not already been processed
    if (processedCycles_.count(cycle)) {
        result.success = false;
        result.rollbackOutcome = RollbackOutcome::SKIPPED;
        result.failureReason = "Duplicate recovery request: cycle already processed.";
        Logger::warn(result.failureReason, "RecoveryManager");
        return result;
    }

    // Step 1: Select victim transaction strictly from the detected cycle
    // Note: selectVictim acquires recoveryMutex_, so perform selection logic inline here
    std::vector<int> uniqueNodes;
    for (int node : cycle) {
        if (std::find(uniqueNodes.begin(), uniqueNodes.end(), node) == uniqueNodes.end()) {
            uniqueNodes.push_back(node);
        }
    }

    if (uniqueNodes.empty()) {
        result.success = false;
        result.failureReason = "Cycle path contains no valid transactions.";
        return result;
    }

    int victimTxId = -1;
    if (policy == VictimPolicy::FEWEST_LOCKS_HELD && lockManager_ != nullptr) {
        size_t minLocks = std::numeric_limits<size_t>::max();
        for (int tx : uniqueNodes) {
            size_t locks = lockManager_->getLocksForTransaction(tx).size();
            if (locks < minLocks || (locks == minLocks && tx > victimTxId)) {
                minLocks = locks;
                victimTxId = tx;
            }
        }
    } else {
        victimTxId = *std::max_element(uniqueNodes.begin(), uniqueNodes.end());
    }

    result.victimTransactionId = victimTxId;

    Logger::error("Deadlock detected", "RecoveryManager");
    Logger::info("Recovery started for cycle " + DeadlockDetector::formatCycle(cycle), "RecoveryManager");
    Logger::info("Selected victim: T" + std::to_string(victimTxId), "RecoveryManager");

    // Step 2: Check if the victim has already committed (handle race condition)
    if (txManager_ && txManager_->isCommitted(victimTxId)) {
        result.success = false;
        result.rollbackOutcome = RollbackOutcome::ALREADY_COMMITTED;
        result.failureReason = "Victim transaction T" + std::to_string(victimTxId) + " is already committed. Cannot rollback.";
        Logger::warn(result.failureReason, "RecoveryManager");
        return result;
    }

    // Step 3: Request safe abort
    Logger::info("Abort requested for T" + std::to_string(victimTxId), "RecoveryManager");

    if (lockManager_) {
        lockManager_->abortWaitingTransaction(victimTxId);
    }

    if (txManager_) {
        txManager_->requestAbort(victimTxId, "Aborted by Deadlock Recovery Manager");
    }

    // Step 4: Database rollback
    bool rollbackSuccess = true;
    if (txManager_) {
        rollbackSuccess = txManager_->rollbackTransaction(victimTxId, "Deadlock recovery victim abort");
        if (rollbackSuccess) {
            Logger::info("Database rollback completed for T" + std::to_string(victimTxId), "RecoveryManager");
            result.rollbackOutcome = RollbackOutcome::SUCCESS;
        } else {
            result.rollbackOutcome = RollbackOutcome::DATABASE_ERROR;
            result.failureReason = "TransactionManager failed to rollback database state for T" + std::to_string(victimTxId);
        }
    } else if (dbManager_) {
        try {
            dbManager_->rollback();
            Logger::info("Database rollback completed for T" + std::to_string(victimTxId), "RecoveryManager");
            result.rollbackOutcome = RollbackOutcome::SUCCESS;
        } catch (const std::exception& ex) {
            result.rollbackOutcome = RollbackOutcome::DATABASE_ERROR;
            result.failureReason = "Database rollback threw exception: " + std::string(ex.what());
            rollbackSuccess = false;
        }
    } else {
        result.rollbackOutcome = RollbackOutcome::NO_TRANSACTION;
    }

    // Step 5: Release victim's locks
    size_t locksHeldBefore = 0;
    if (lockManager_) {
        locksHeldBefore = lockManager_->getLocksForTransaction(victimTxId).size();
        lockManager_->releaseAllLocks(victimTxId);
    }
    result.locksReleased = locksHeldBefore;
    Logger::info("Released " + std::to_string(locksHeldBefore) + " locks held by T" + std::to_string(victimTxId), "RecoveryManager");

    // Step 6: Wake and record surviving waiting transactions
    for (int node : uniqueNodes) {
        if (node != victimTxId) {
            Logger::info("Waiting transaction T" + std::to_string(node) + " notified", "RecoveryManager");
            result.waitingTransactionsNotified++;
        }
    }

    if (!rollbackSuccess && result.rollbackOutcome == RollbackOutcome::DATABASE_ERROR) {
        result.success = false;
        Logger::warn("Recovery failed during database rollback", "RecoveryManager");
        return result;
    }

    result.success = true;
    totalRecoveries_++;
    processedCycles_.insert(cycle);

    Logger::info("Recovery completed", "RecoveryManager");
    return result;
}

RecoveryResult RecoveryManager::recover(const std::vector<int>& cycle, VictimPolicy policy) {
    DeadlockReport report;
    report.deadlockDetected = !cycle.empty();
    if (!cycle.empty()) {
        report.cycles.push_back(cycle);
    }
    return recover(report, policy);
}

bool RecoveryManager::recoverDeadlock(int victimTxId, const std::string& reason) {
    std::lock_guard<std::mutex> lock(recoveryMutex_);
    Logger::info("Executing recovery rollback for victim Tx #" + std::to_string(victimTxId) + 
                 " Reason: " + reason, "RecoveryManager");

    if (lockManager_) {
        lockManager_->abortWaitingTransaction(victimTxId);
        lockManager_->releaseAllLocks(victimTxId);
    }

    if (txManager_) {
        txManager_->rollbackTransaction(victimTxId, reason);
    }

    totalRecoveries_++;
    return true;
}

size_t RecoveryManager::getRecoveryCount() const noexcept {
    std::lock_guard<std::mutex> lock(recoveryMutex_);
    return totalRecoveries_;
}

void RecoveryManager::resetRecoveryCount() noexcept {
    std::lock_guard<std::mutex> lock(recoveryMutex_);
    totalRecoveries_ = 0;
    processedCycles_.clear();
}

RecoveryResult RecoveryManager::demonstrateControlledDeadlockRecovery(LockManager& lm, DatabaseManager* db) {
    Logger::info("Starting Controlled Deadlock Recovery Demonstration...", "RecoveryManager");

    const int acc1 = 101;
    const int acc2 = 102;
    const int tx1 = 1;
    const int tx2 = 2;

    // Reset clean lock state
    lm.releaseAllLocks(tx1);
    lm.releaseAllLocks(tx2);

    // Initial lock acquisitions:
    // T1 holds X(A101)
    // T2 holds X(A102)
    assert(lm.acquireLock(tx1, acc1, LockMode::EXCLUSIVE));
    assert(lm.acquireLock(tx2, acc2, LockMode::EXCLUSIVE));

    std::atomic<bool> t1Finished{false};
    std::atomic<bool> t2Finished{false};
    std::atomic<bool> t1AcquiredAcc2{false};
    std::atomic<bool> t2AcquiredAcc1{false};
    std::atomic<bool> t2Aborted{false};

    // Thread 1 (Survivor): requests X(A102) -> blocks
    std::thread th1([&]() {
        if (lm.acquireLock(tx1, acc2, LockMode::EXCLUSIVE)) {
            t1AcquiredAcc2.store(true);
            // Simulate transfer work
            std::this_thread::yield();
            lm.releaseAllLocks(tx1);
        }
        t1Finished.store(true);
    });

    // Wait deterministically until T1 is in waiting queue of acc2
    while (lm.getWaitingRequests(acc2).empty()) {
        std::this_thread::yield();
    }

    // Thread 2 (Victim): requests X(A101) -> blocks
    std::thread th2([&]() {
        if (lm.acquireLock(tx2, acc1, LockMode::EXCLUSIVE)) {
            t2AcquiredAcc1.store(true);
            lm.releaseAllLocks(tx2);
        } else {
            // Lock acquisition failed due to deadlock abort
            t2Aborted.store(true);
            lm.releaseAllLocks(tx2);
        }
        t2Finished.store(true);
    });

    // Wait deterministically until T2 is in waiting queue of acc1
    while (lm.getWaitingRequests(acc1).empty()) {
        std::this_thread::yield();
    }

    // Both threads are actively waiting in queues -> Circular wait
    DeadlockDetector detector(&lm);
    DeadlockReport dlReport = detector.detectDeadlocks();
    assert(dlReport.deadlockDetected);

    // Coordinate recovery: select victim (T2, since tx2 > tx1), abort T2, release locks
    RecoveryManager recoveryMgr(&lm, nullptr, db);
    RecoveryResult recResult = recoveryMgr.recover(dlReport, VictimPolicy::YOUNGEST_TRANSACTION);

    assert(recResult.success);
    assert(recResult.victimTransactionId == tx2);

    // Join threads safely without hangs
    th2.join();
    th1.join();

    assert(t2Finished.load());
    assert(t1Finished.load());
    assert(t1AcquiredAcc2.load()); // Survivor acquired lock on acc2
    assert(t2Aborted.load());       // Victim detected abort and unwound

    // Clean up locks
    lm.releaseAllLocks(tx1);
    lm.releaseAllLocks(tx2);

    Logger::info("Controlled Deadlock Recovery Demonstration Completed Safely", "RecoveryManager");
    return recResult;
}

} // namespace paylock
