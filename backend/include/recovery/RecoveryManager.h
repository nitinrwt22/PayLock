#ifndef PAYLOCK_RECOVERY_MANAGER_H
#define PAYLOCK_RECOVERY_MANAGER_H

#include <vector>
#include <string>
#include <mutex>

namespace paylock {

enum class VictimPolicy {
    YOUNGEST_TRANSACTION,     // Abort most recently started transaction
    FEWEST_LOCKS_HELD,        // Abort transaction holding least resources
    LOWEST_COST_TO_ROLLBACK   // Abort transaction with least work performed
};

std::string victimPolicyToString(VictimPolicy policy);

/**
 * @brief Coordinates victim selection and transaction rollback upon deadlock detection.
 * 
 * OS & DBMS Concept:
 * - Deadlock Recovery via Process / Transaction Termination.
 * - Rolling back victim to a checkpoint and releasing all held locks.
 */
class RecoveryManager {
public:
    RecoveryManager() = default;

    int selectVictim(const std::vector<int>& cyclePath, VictimPolicy policy = VictimPolicy::YOUNGEST_TRANSACTION);
    bool recoverDeadlock(int victimTxId, const std::string& reason = "Deadlock cycle resolution");

    size_t getRecoveryCount() const noexcept;

private:
    mutable std::mutex recoveryMutex_;
    size_t totalRecoveries_{0};
};

} // namespace paylock

#endif // PAYLOCK_RECOVERY_MANAGER_H
