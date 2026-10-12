#ifndef PAYLOCK_TRANSACTION_MANAGER_H
#define PAYLOCK_TRANSACTION_MANAGER_H

#include "../database/DatabaseManager.h"
#include <memory>
#include <vector>
#include <optional>
#include <string>

namespace paylock {

/**
 * @brief TransactionManager acts as the coordinator for core payment business transactions.
 * 
 * Day 2 Implementation:
 * - Orchestrates atomic financial operations (transfer, deposit, withdraw).
 * - Implements MySQL transaction lifecycle: START TRANSACTION -> Operations -> COMMIT / ROLLBACK.
 * - Enforces ACID properties (Atomicity, Consistency, Durability, single-threaded Isolation foundation).
 * - Manages transaction states: PENDING, PROCESSING, COMMITTED, ROLLED_BACK, FAILED.
 * - Records detailed audit logs in transaction_logs for full traceability.
 * 
 * Day 3 Readiness:
 * - Thread-ready design: Operations rely on the thread-safe DatabaseManager infrastructure.
 * - Decoupled business logic allows multiple worker threads to invoke transfer/deposit/withdraw.
 */
class TransactionManager {
public:
    explicit TransactionManager(DatabaseManager& dbManager);

    // Core Payment Operations (Business Logic)
    bool transfer(int senderAccountId, int receiverAccountId, double amount);
    bool transferFunds(int senderAccountId, int receiverAccountId, double amount) {
        return transfer(senderAccountId, receiverAccountId, amount);
    }
    bool deposit(int accountId, double amount);
    bool withdraw(int accountId, double amount);

    // Transaction Queries
    std::optional<Transaction> getTransaction(int transactionId);
    std::vector<Transaction> getTransactionHistory(int accountId = 0);
    std::vector<TransactionLog> getTransactionLogs(int transactionId = 0);
    std::vector<Transaction> getAllTransactions();

    // Account Queries
    std::optional<Account> getAccount(int accountId);
    std::optional<double> checkBalance(int accountId);
    std::vector<Account> getAllAccounts();

    // Visual Terminal Display Functions
    void displayTransactionHistory() const;
    void displayAccounts() const;
    void displayTransactionAudit(int transactionId) const;

    // Diagnostics
    std::string getLastError() const;

private:
    DatabaseManager& db_;
    mutable std::string lastError_;

    void setLastError(const std::string& err) const;
    static int64_t toCents(double amount);
    static std::string formatAmount(double amount);
};

} // namespace paylock

#endif // PAYLOCK_TRANSACTION_MANAGER_H
