#ifndef PAYLOCK_LOCK_MANAGER_H
#define PAYLOCK_LOCK_MANAGER_H

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <optional>
#include <iostream>

namespace paylock {

/**
 * @brief Lock Mode according to 2-Phase Locking (2PL) principles.
 * 
 * DBMS Concept: Concurrency Control & Granular Locking.
 * - SHARED: Read Lock (S). Compatible with other SHARED requests.
 * - EXCLUSIVE: Write Lock (X). Conflicting with all other locks.
 */
enum class LockMode {
    SHARED,    // Read Lock (S)
    EXCLUSIVE  // Write Lock (X)
};

/**
 * @brief Current lifecycle state of a lock request.
 */
enum class LockState {
    GRANTED,
    WAITING,
    RELEASED
};

std::string lockModeToString(LockMode mode);
std::string lockStateToString(LockState state);
std::string formatResource(int accountId);

/**
 * @brief Represents an individual lock request / ownership record.
 */
struct LockRequest {
    int transactionId{0};
    int accountId{0};
    LockMode mode{LockMode::SHARED};
    LockState state{LockState::WAITING};
    std::chrono::system_clock::time_point requestTime;

    LockRequest() = default;
    LockRequest(int txId, int accId, LockMode m, LockState s = LockState::WAITING)
        : transactionId(txId), accountId(accId), mode(m), state(s),
          requestTime(std::chrono::system_clock::now()) {}
};

/**
 * @brief Lock Table Entry for a single resource (e.g. ACCOUNT:A101).
 * Tracks active lock holders and queued wait requests.
 */
struct ResourceLockEntry {
    int accountId{0};
    std::optional<LockMode> currentMode{std::nullopt};
    std::unordered_set<int> holders; // transaction IDs holding the lock
    std::vector<LockRequest> waitQueue; // transactions waiting in FIFO order
};

/**
 * @brief LockManager provides fine-grained, account-level concurrency control
 * between transactions, supporting 2-Phase Locking (2PL), Shared/Exclusive locks,
 * wait queues with condition variables, lock upgrades, and lock monitoring.
 * 
 * OS Concepts Demonstrated:
 * - Thread-safe Lock Table protected by std::mutex (protects internal metadata).
 * - Non-busy wait queue management using std::condition_variable.
 * - Explicit Lock Ownership tracking (Transaction ID -> Held Resource Locks).
 * - Deterministic resource ordering strategy to prevent AB-BA deadlocks in normal transfers.
 * 
 * DBMS Concepts Demonstrated:
 * - Shared (S) and Exclusive (X) lock semantics.
 * - Strict Compatibility Matrix enforcement (S+S compatible, X conflicts with S and X).
 * - Lock Upgrade (S -> X) when no conflicting transaction holds a lock.
 * - Logical resource locking (ACCOUNT:A101) allowing independent accounts to operate concurrently.
 */
class LockManager {
public:
    LockManager() = default;
    ~LockManager();

    // Non-copyable, non-movable
    LockManager(const LockManager&) = delete;
    LockManager& operator=(const LockManager&) = delete;

    // Lock Compatibility Checker
    static bool isCompatible(LockMode requested, const std::optional<LockMode>& currentMode, size_t numHolders);

    // Lock Acquisition Operations
    bool acquireLock(int transactionId, int accountId, LockMode mode, int timeoutMs = 0);
    bool tryAcquireLock(int transactionId, int accountId, LockMode mode);

    // Deterministic Multi-Resource Acquisition (Prevents AB-BA Deadlock)
    bool acquireTransferLocks(int transactionId, int account1, int account2, int timeoutMs = 0);

    // Lock Release Operations
    bool releaseLock(int transactionId, int accountId);
    void releaseAllLocks(int transactionId);

    // Lock Upgrade Operation (S -> X)
    bool upgradeLock(int transactionId, int accountId, int timeoutMs = 0);

    // Query & State Inspection
    bool isAccountLocked(int accountId) const;
    std::optional<LockMode> getAccountLockMode(int accountId) const;
    std::vector<int> getLockHolders(int accountId) const;
    std::vector<LockRequest> getWaitingRequests(int accountId) const;
    std::vector<int> getLocksForTransaction(int transactionId) const;
    std::vector<std::pair<int, LockMode>> getDetailedLocksForTransaction(int transactionId) const;

    // Monitoring & Display
    void displayLockTable() const;

private:
    bool canGrantImmediately(int transactionId, int accountId, LockMode requestedMode) const;

    mutable std::mutex tableMutex_; // Mutex protecting the internal Lock Table
    std::condition_variable lockCv_; // Condition variable for waking waiting transactions

    // accountId -> ResourceLockEntry
    std::unordered_map<int, ResourceLockEntry> lockTable_;

    // transactionId -> set of account IDs held
    std::unordered_map<int, std::unordered_map<int, LockMode>> transactionHoldings_;
};

} // namespace paylock

#endif // PAYLOCK_LOCK_MANAGER_H
