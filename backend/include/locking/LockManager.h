#ifndef PAYLOCK_LOCK_MANAGER_H
#define PAYLOCK_LOCK_MANAGER_H

#include <string>
#include <vector>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

namespace paylock {

/**
 * @brief Lock Mode according to 2-Phase Locking (2PL) principles.
 * 
 * DBMS Concept: Concurrency Control & Granular Locking.
 */
enum class LockMode {
    SHARED,    // Read Lock (S)
    EXCLUSIVE  // Write Lock (X)
};

std::string lockModeToString(LockMode mode);

struct LockRequest {
    int transactionId;
    int accountId;
    LockMode mode;
    bool granted{false};
};

/**
 * @brief LockManager provides fine-grained account-level locking for transactions.
 * 
 * Future Phase (Day 4):
 * - Shared (S) and Exclusive (X) locks per account.
 * - Compatibility matrix enforcement (S-S compatible, X-S / X-X conflicting).
 * - Wait-queues for blocked transactions.
 * - Strict 2-Phase Locking (Strict 2PL).
 */
class LockManager {
public:
    LockManager() = default;

    // Interface methods (Stubs for future phase)
    bool acquireLock(int transactionId, int accountId, LockMode mode);
    bool releaseLock(int transactionId, int accountId);
    void releaseAllLocks(int transactionId);

    bool isAccountLocked(int accountId) const;
    std::vector<int> getLockHolders(int accountId) const;
    std::vector<LockRequest> getWaitingRequests(int accountId) const;

private:
    mutable std::mutex lockMutex_;
    
    // accountId -> set of holding transaction IDs
    std::unordered_map<int, std::unordered_set<int>> activeLocks_;
    // accountId -> lock mode
    std::unordered_map<int, LockMode> accountLockModes_;
};

} // namespace paylock

#endif // PAYLOCK_LOCK_MANAGER_H
