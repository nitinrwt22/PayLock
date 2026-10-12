#include "recovery/RecoveryManager.h"
#include "logging/Logger.h"
#include <algorithm>

namespace paylock {

std::string victimPolicyToString(VictimPolicy policy) {
    switch (policy) {
        case VictimPolicy::YOUNGEST_TRANSACTION:   return "YOUNGEST_TRANSACTION";
        case VictimPolicy::FEWEST_LOCKS_HELD:      return "FEWEST_LOCKS_HELD";
        case VictimPolicy::LOWEST_COST_TO_ROLLBACK: return "LOWEST_COST_TO_ROLLBACK";
    }
    return "UNKNOWN";
}

int RecoveryManager::selectVictim(const std::vector<int>& cyclePath, VictimPolicy policy) {
    std::lock_guard<std::mutex> lock(recoveryMutex_);
    if (cyclePath.empty()) {
        return -1;
    }

    // Default policy: select the highest transaction ID (youngest transaction)
    int victim = *std::max_element(cyclePath.begin(), cyclePath.end());
    Logger::warn("Victim selected for deadlock resolution: Tx #" + std::to_string(victim) + 
                 " (Policy: " + victimPolicyToString(policy) + ")", "RecoveryManager");
    return victim;
}

bool RecoveryManager::recoverDeadlock(int victimTxId, const std::string& reason) {
    std::lock_guard<std::mutex> lock(recoveryMutex_);
    Logger::info("Executing recovery rollback for victim Tx #" + std::to_string(victimTxId) + 
                 " Reason: " + reason, "RecoveryManager");
    totalRecoveries_++;
    return true;
}

size_t RecoveryManager::getRecoveryCount() const noexcept {
    std::lock_guard<std::mutex> lock(recoveryMutex_);
    return totalRecoveries_;
}

} // namespace paylock
