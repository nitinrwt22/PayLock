#include "concurrency/ConcurrencyManager.h"
#include "logging/Logger.h"

namespace paylock {

ConcurrencyManager::ConcurrencyManager(DatabaseConfig config)
    : processor_(std::move(config)) {}

ConcurrencyMetrics ConcurrencyManager::executeBatch(const std::vector<TransferRequest>& transfers) {
    Logger::info("Dispatching batch of " + std::to_string(transfers.size()) + " concurrent transfers", "ConcurrencyManager");
    return processor_.processConcurrentTransfers(transfers);
}

ConcurrencyMetrics ConcurrencyManager::executeWithdrawals(const std::vector<WithdrawalRequest>& withdrawals) {
    Logger::info("Dispatching batch of " + std::to_string(withdrawals.size()) + " concurrent withdrawals", "ConcurrencyManager");
    return processor_.processConcurrentWithdrawals(withdrawals);
}

void ConcurrencyManager::runRaceConditionDemo(double balance, double w1, double w2) {
    Logger::info("Executing Race Condition demonstration", "ConcurrencyManager");
    processor_.demonstrateRaceCondition(balance, w1, w2);
}

ConcurrentTransactionProcessor& ConcurrencyManager::getProcessor() noexcept {
    return processor_;
}

} // namespace paylock
