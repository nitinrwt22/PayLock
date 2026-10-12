#include "locking/LockManager.h"
#include "logging/Logger.h"

namespace paylock {

std::string lockModeToString(LockMode mode) {
    switch (mode) {
        case LockMode::SHARED:    return "SHARED";
        case LockMode::EXCLUSIVE: return "EXCLUSIVE";
    }
    return "UNKNOWN";
}

bool LockManager::acquireLock(int transactionId, int accountId, LockMode mode) {
    std::lock_guard<std::mutex> lock(lockMutex_);
    Logger::info("Acquiring " + lockModeToString(mode) + " lock on Account " + 
                 std::to_string(accountId) + " for Tx #" + std::to_string(transactionId), "LockManager");

    // [STUB for Day 4]: Currently records single exclusive ownership for demonstration
    activeLocks_[accountId].insert(transactionId);
    accountLockModes_[accountId] = mode;
    return true;
}

bool LockManager::releaseLock(int transactionId, int accountId) {
    std::lock_guard<std::mutex> lock(lockMutex_);
    Logger::info("Releasing lock on Account " + std::to_string(accountId) + 
                 " for Tx #" + std::to_string(transactionId), "LockManager");

    auto it = activeLocks_.find(accountId);
    if (it != activeLocks_.end()) {
        it->second.erase(transactionId);
        if (it->second.empty()) {
            activeLocks_.erase(it);
            accountLockModes_.erase(accountId);
        }
        return true;
    }
    return false;
}

void LockManager::releaseAllLocks(int transactionId) {
    std::lock_guard<std::mutex> lock(lockMutex_);
    Logger::info("Releasing all held locks for Tx #" + std::to_string(transactionId), "LockManager");

    for (auto it = activeLocks_.begin(); it != activeLocks_.end(); ) {
        it->second.erase(transactionId);
        if (it->second.empty()) {
            accountLockModes_.erase(it->first);
            it = activeLocks_.erase(it);
        } else {
            ++it;
        }
    }
}

bool LockManager::isAccountLocked(int accountId) const {
    std::lock_guard<std::mutex> lock(lockMutex_);
    return activeLocks_.find(accountId) != activeLocks_.end();
}

std::vector<int> getLockHolders(int accountId);
std::vector<int> LockManager::getLockHolders(int accountId) const {
    std::lock_guard<std::mutex> lock(lockMutex_);
    std::vector<int> holders;
    auto it = activeLocks_.find(accountId);
    if (it != activeLocks_.end()) {
        holders.assign(it->second.begin(), it->second.end());
    }
    return holders;
}

std::vector<LockRequest> LockManager::getWaitingRequests(int /*accountId*/) const {
    // [PLANNED for Day 4 Wait Queue]
    return {};
}

} // namespace paylock
