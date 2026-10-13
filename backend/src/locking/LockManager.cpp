#include "locking/LockManager.h"
#include "logging/Logger.h"
#include <iomanip>
#include <sstream>
#include <algorithm>

namespace paylock {

std::string lockModeToString(LockMode mode) {
    switch (mode) {
        case LockMode::SHARED:    return "SHARED";
        case LockMode::EXCLUSIVE: return "EXCLUSIVE";
    }
    return "UNKNOWN";
}

std::string lockStateToString(LockState state) {
    switch (state) {
        case LockState::GRANTED:  return "GRANTED";
        case LockState::WAITING:  return "WAITING";
        case LockState::RELEASED: return "RELEASED";
    }
    return "UNKNOWN";
}

std::string formatResource(int accountId) {
    return "ACCOUNT:A" + std::to_string(accountId);
}

LockManager::~LockManager() {
    std::lock_guard<std::mutex> lock(tableMutex_);
    lockTable_.clear();
    transactionHoldings_.clear();
}

bool LockManager::isCompatible(LockMode requested, const std::optional<LockMode>& currentMode, size_t numHolders) {
    if (!currentMode.has_value() || numHolders == 0) {
        return true;
    }
    if (requested == LockMode::SHARED && *currentMode == LockMode::SHARED) {
        return true;
    }
    return false;
}

bool LockManager::canGrantImmediately(int transactionId, int accountId, LockMode requestedMode) const {
    auto it = lockTable_.find(accountId);
    if (it == lockTable_.end() || it->second.holders.empty()) {
        return true;
    }

    const auto& entry = it->second;

    // Check if the transaction already holds this lock
    if (entry.holders.count(transactionId)) {
        if (*entry.currentMode == requestedMode) {
            return true; // Reentrant grant
        }
        if (requestedMode == LockMode::EXCLUSIVE && *entry.currentMode == LockMode::SHARED) {
            // Can only upgrade immediately if this transaction is the SOLE holder
            return (entry.holders.size() == 1);
        }
    }

    // Strict FIFO queue check: do not jump ahead of earlier waiting transactions
    if (!entry.waitQueue.empty() && entry.waitQueue.front().transactionId != transactionId) {
        return false;
    }

    return isCompatible(requestedMode, entry.currentMode, entry.holders.size());
}

bool LockManager::acquireLock(int transactionId, int accountId, LockMode mode, int timeoutMs) {
    std::unique_lock<std::mutex> lock(tableMutex_);

    Logger::info("[LOCK] T" + std::to_string(transactionId) + " requesting " + 
                 lockModeToString(mode) + "(" + formatResource(accountId) + ")", "LockManager");

    auto& entry = lockTable_[accountId];
    entry.accountId = accountId;

    // Check if transaction already holds the exact lock
    if (entry.holders.count(transactionId)) {
        if (entry.currentMode.has_value() && *entry.currentMode == mode) {
            return true;
        }
        if (mode == LockMode::EXCLUSIVE && entry.currentMode == LockMode::SHARED) {
            lock.unlock();
            return upgradeLock(transactionId, accountId, timeoutMs);
        }
    }

    // If resource is free or compatible and no earlier waiters, grant immediately
    if (canGrantImmediately(transactionId, accountId, mode)) {
        entry.holders.insert(transactionId);
        entry.currentMode = mode;
        transactionHoldings_[transactionId][accountId] = mode;

        Logger::info("[LOCK] T" + std::to_string(transactionId) + " granted " + 
                     lockModeToString(mode) + "(" + formatResource(accountId) + ")", "LockManager");
        return true;
    }

    // Incompatible: Place request into waiting queue
    entry.waitQueue.emplace_back(transactionId, accountId, mode, LockState::WAITING);
    Logger::info("[LOCK] T" + std::to_string(transactionId) + " waiting for " + 
                 lockModeToString(mode) + "(" + formatResource(accountId) + ")", "LockManager");

    auto pred = [this, transactionId, accountId, mode]() {
        auto it = lockTable_.find(accountId);
        if (it == lockTable_.end()) return false;
        const auto& e = it->second;
        if (e.waitQueue.empty() || e.waitQueue.front().transactionId != transactionId) {
            return false;
        }
        return isCompatible(mode, e.currentMode, e.holders.size());
    };

    bool acquired = false;
    if (timeoutMs > 0) {
        acquired = lockCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), pred);
    } else {
        lockCv_.wait(lock, pred);
        acquired = true;
    }

    if (acquired) {
        auto& entryRef = lockTable_[accountId];
        // Pop request from front of queue
        auto qIt = std::find_if(entryRef.waitQueue.begin(), entryRef.waitQueue.end(), 
            [transactionId](const LockRequest& r) { return r.transactionId == transactionId; });
        if (qIt != entryRef.waitQueue.end()) {
            entryRef.waitQueue.erase(qIt);
        }

        entryRef.holders.insert(transactionId);
        entryRef.currentMode = mode;
        transactionHoldings_[transactionId][accountId] = mode;

        Logger::info("[LOCK] T" + std::to_string(transactionId) + " granted " + 
                     lockModeToString(mode) + "(" + formatResource(accountId) + ")", "LockManager");

        // If granted SHARED lock, wake other compatible shared waiters
        if (mode == LockMode::SHARED) {
            lockCv_.notify_all();
        }
        return true;
    }

    // Timed out: remove from wait queue
    auto& entryRef = lockTable_[accountId];
    auto qIt = std::find_if(entryRef.waitQueue.begin(), entryRef.waitQueue.end(), 
        [transactionId](const LockRequest& r) { return r.transactionId == transactionId; });
    if (qIt != entryRef.waitQueue.end()) {
        entryRef.waitQueue.erase(qIt);
    }

    Logger::warn("[LOCK] T" + std::to_string(transactionId) + " timed out waiting for " + 
                 lockModeToString(mode) + "(" + formatResource(accountId) + ")", "LockManager");
    return false;
}

bool LockManager::tryAcquireLock(int transactionId, int accountId, LockMode mode) {
    std::lock_guard<std::mutex> lock(tableMutex_);

    if (!canGrantImmediately(transactionId, accountId, mode)) {
        return false;
    }

    auto& entry = lockTable_[accountId];
    entry.accountId = accountId;
    entry.holders.insert(transactionId);
    entry.currentMode = mode;
    transactionHoldings_[transactionId][accountId] = mode;

    Logger::info("[LOCK] T" + std::to_string(transactionId) + " granted (tryAcquire) " + 
                 lockModeToString(mode) + "(" + formatResource(accountId) + ")", "LockManager");
    return true;
}

bool LockManager::acquireTransferLocks(int transactionId, int account1, int account2, int timeoutMs) {
    if (account1 <= 0 && account2 <= 0) {
        return true;
    }
    if (account1 <= 0) {
        return acquireLock(transactionId, account2, LockMode::EXCLUSIVE, timeoutMs);
    }
    if (account2 <= 0) {
        return acquireLock(transactionId, account1, LockMode::EXCLUSIVE, timeoutMs);
    }
    if (account1 == account2) {
        return acquireLock(transactionId, account1, LockMode::EXCLUSIVE, timeoutMs);
    }

    // Deterministic Lock Ordering: Lower account ID first to prevent circular waits
    int first = std::min(account1, account2);
    int second = std::max(account1, account2);

    if (!acquireLock(transactionId, first, LockMode::EXCLUSIVE, timeoutMs)) {
        return false;
    }

    if (!acquireLock(transactionId, second, LockMode::EXCLUSIVE, timeoutMs)) {
        releaseLock(transactionId, first);
        return false;
    }

    return true;
}

bool LockManager::releaseLock(int transactionId, int accountId) {
    std::unique_lock<std::mutex> lock(tableMutex_);

    auto it = lockTable_.find(accountId);
    if (it == lockTable_.end() || it->second.holders.find(transactionId) == it->second.holders.end()) {
        return false;
    }

    auto& entry = it->second;
    entry.holders.erase(transactionId);
    transactionHoldings_[transactionId].erase(accountId);
    if (transactionHoldings_[transactionId].empty()) {
        transactionHoldings_.erase(transactionId);
    }

    if (entry.holders.empty()) {
        entry.currentMode = std::nullopt;
    }

    Logger::info("[LOCK] T" + std::to_string(transactionId) + " released lock on " + formatResource(accountId), "LockManager");

    // Re-evaluate waiting queue and notify waiting transactions
    lockCv_.notify_all();
    return true;
}

void LockManager::releaseAllLocks(int transactionId) {
    std::unique_lock<std::mutex> lock(tableMutex_);

    auto txIt = transactionHoldings_.find(transactionId);
    if (txIt == transactionHoldings_.end()) {
        return;
    }

    Logger::info("[LOCK] Releasing all held locks for Tx #" + std::to_string(transactionId), "LockManager");

    std::vector<int> heldAccounts;
    for (const auto& kv : txIt->second) {
        heldAccounts.push_back(kv.first);
    }

    for (int accId : heldAccounts) {
        auto it = lockTable_.find(accId);
        if (it != lockTable_.end()) {
            it->second.holders.erase(transactionId);
            if (it->second.holders.empty()) {
                it->second.currentMode = std::nullopt;
            }
            Logger::info("[LOCK] T" + std::to_string(transactionId) + " released lock on " + formatResource(accId), "LockManager");
        }
    }

    transactionHoldings_.erase(transactionId);
    lockCv_.notify_all();
}

bool LockManager::upgradeLock(int transactionId, int accountId, int timeoutMs) {
    std::unique_lock<std::mutex> lock(tableMutex_);

    auto it = lockTable_.find(accountId);
    if (it == lockTable_.end() || it->second.holders.find(transactionId) == it->second.holders.end()) {
        Logger::warn("[LOCK] Cannot upgrade lock: Tx #" + std::to_string(transactionId) + " does not hold a lock on " + formatResource(accountId), "LockManager");
        return false;
    }

    auto& entry = it->second;
    if (entry.currentMode == LockMode::EXCLUSIVE) {
        return true; // Already exclusive
    }

    Logger::info("[LOCK] T" + std::to_string(transactionId) + " requesting UPGRADE (SHARED -> EXCLUSIVE) on " + formatResource(accountId), "LockManager");

    // If this transaction is the SOLE holder of the shared lock, upgrade immediately!
    if (entry.holders.size() == 1) {
        entry.currentMode = LockMode::EXCLUSIVE;
        transactionHoldings_[transactionId][accountId] = LockMode::EXCLUSIVE;
        Logger::info("[LOCK] T" + std::to_string(transactionId) + " upgraded SHARED -> EXCLUSIVE on " + formatResource(accountId), "LockManager");
        return true;
    }

    // Other transactions also hold SHARED lock: must wait until all other holders release
    entry.waitQueue.emplace_back(transactionId, accountId, LockMode::EXCLUSIVE, LockState::WAITING);

    auto pred = [this, transactionId, accountId]() {
        auto it = lockTable_.find(accountId);
        if (it == lockTable_.end()) return false;
        const auto& e = it->second;
        return (e.holders.size() == 1 && e.holders.count(transactionId));
    };

    bool upgraded = false;
    if (timeoutMs > 0) {
        upgraded = lockCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), pred);
    } else {
        lockCv_.wait(lock, pred);
        upgraded = true;
    }

    // Remove upgrade request from wait queue
    auto& entryRef = lockTable_[accountId];
    auto qIt = std::find_if(entryRef.waitQueue.begin(), entryRef.waitQueue.end(),
        [transactionId](const LockRequest& r) { return r.transactionId == transactionId; });
    if (qIt != entryRef.waitQueue.end()) {
        entryRef.waitQueue.erase(qIt);
    }

    if (upgraded) {
        entryRef.currentMode = LockMode::EXCLUSIVE;
        transactionHoldings_[transactionId][accountId] = LockMode::EXCLUSIVE;
        Logger::info("[LOCK] T" + std::to_string(transactionId) + " upgraded SHARED -> EXCLUSIVE on " + formatResource(accountId), "LockManager");
        return true;
    }

    Logger::warn("[LOCK] T" + std::to_string(transactionId) + " timed out waiting to upgrade on " + formatResource(accountId), "LockManager");
    return false;
}

bool LockManager::isAccountLocked(int accountId) const {
    std::lock_guard<std::mutex> lock(tableMutex_);
    auto it = lockTable_.find(accountId);
    return (it != lockTable_.end() && !it->second.holders.empty());
}

std::optional<LockMode> LockManager::getAccountLockMode(int accountId) const {
    std::lock_guard<std::mutex> lock(tableMutex_);
    auto it = lockTable_.find(accountId);
    if (it != lockTable_.end() && !it->second.holders.empty()) {
        return it->second.currentMode;
    }
    return std::nullopt;
}

std::vector<int> LockManager::getLockHolders(int accountId) const {
    std::lock_guard<std::mutex> lock(tableMutex_);
    auto it = lockTable_.find(accountId);
    if (it != lockTable_.end()) {
        return std::vector<int>(it->second.holders.begin(), it->second.holders.end());
    }
    return {};
}

std::vector<LockRequest> LockManager::getWaitingRequests(int accountId) const {
    std::lock_guard<std::mutex> lock(tableMutex_);
    auto it = lockTable_.find(accountId);
    if (it != lockTable_.end()) {
        return it->second.waitQueue;
    }
    return {};
}

std::vector<int> LockManager::getLocksForTransaction(int transactionId) const {
    std::lock_guard<std::mutex> lock(tableMutex_);
    std::vector<int> held;
    auto it = transactionHoldings_.find(transactionId);
    if (it != transactionHoldings_.end()) {
        for (const auto& kv : it->second) {
            held.push_back(kv.first);
        }
    }
    return held;
}

std::vector<std::pair<int, LockMode>> LockManager::getDetailedLocksForTransaction(int transactionId) const {
    std::lock_guard<std::mutex> lock(tableMutex_);
    std::vector<std::pair<int, LockMode>> detailed;
    auto it = transactionHoldings_.find(transactionId);
    if (it != transactionHoldings_.end()) {
        for (const auto& kv : it->second) {
            detailed.emplace_back(kv.first, kv.second);
        }
    }
    return detailed;
}

void LockManager::displayLockTable() const {
    std::lock_guard<std::mutex> lock(tableMutex_);

    std::cout << "\n=================================================================\n";
    std::cout << "                      PAYLOCK LOCK TABLE                         \n";
    std::cout << "=================================================================\n";
    std::cout << std::left 
              << std::setw(16) << "RESOURCE"
              << std::setw(12) << "HOLDERS"
              << std::setw(12) << "TYPE"
              << std::setw(12) << "STATE"
              << "WAITING QUEUE\n";
    std::cout << "-----------------------------------------------------------------\n";

    if (lockTable_.empty()) {
        std::cout << " (No active locks or waiting requests in the Lock Table)\n";
    }

    for (const auto& kv : lockTable_) {
        int accId = kv.first;
        const auto& entry = kv.second;

        std::string resStr = formatResource(accId);
        std::string modeStr = entry.currentMode ? lockModeToString(*entry.currentMode) : "NONE";
        std::string stateStr = entry.holders.empty() ? "FREE" : "GRANTED";

        std::ostringstream holdersSs;
        if (entry.holders.empty()) {
            holdersSs << "None";
        } else {
            bool first = true;
            for (int h : entry.holders) {
                if (!first) holdersSs << ", ";
                holdersSs << "T" << h;
                first = false;
            }
        }

        std::ostringstream waitSs;
        if (entry.waitQueue.empty()) {
            waitSs << "None";
        } else {
            bool first = true;
            for (const auto& req : entry.waitQueue) {
                if (!first) waitSs << " -> ";
                waitSs << "T" << req.transactionId << " (" << lockModeToString(req.mode) << ")";
                first = false;
            }
        }

        std::cout << std::left
                  << std::setw(16) << resStr
                  << std::setw(12) << holdersSs.str()
                  << std::setw(12) << modeStr
                  << std::setw(12) << stateStr
                  << waitSs.str() << "\n";
    }
    std::cout << "=================================================================\n\n";
}

LockTableSnapshot LockManager::getSnapshot() const {
    std::lock_guard<std::mutex> lock(tableMutex_);
    LockTableSnapshot snapshot;
    snapshot.snapshotTime = std::chrono::system_clock::now();
    snapshot.transactionHoldings = transactionHoldings_;

    for (const auto& kv : lockTable_) {
        const auto& entry = kv.second;
        // Include entries that currently have active holders or waiters
        if (!entry.holders.empty() || !entry.waitQueue.empty()) {
            ResourceSnapshot res;
            res.accountId = entry.accountId;
            res.currentMode = entry.currentMode;
            res.holders = entry.holders;
            res.waitQueue = entry.waitQueue;
            snapshot.resources[entry.accountId] = std::move(res);
        }
    }
    return snapshot;
}

} // namespace paylock
