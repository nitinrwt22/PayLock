#ifndef PAYLOCK_TRANSACTION_MANAGER_H
#define PAYLOCK_TRANSACTION_MANAGER_H

#include "database/DatabaseManager.h"
#include "models/Transaction.h"
#include "models/Account.h"
#include "models/TransactionLog.h"
#include <memory>
#include <vector>
#include <optional>
#include <string>

namespace paylock {

/**
 * @brief TransactionManager coordinates core financial business transactions.
 * 
 * Enforces ACID properties in conjunction with DatabaseManager:
 * - Atomicity: Entire transfer, deposit, or withdrawal commits or rolls back as a unit.
 * - Consistency: Balances are checked, non-negative constraints enforced, and accounts validated.
 * - Isolation: Database transactions prevent dirty reads and uncommitted state exposure.
 * - Durability: MySQL InnoDB engine guarantees committed transactions persist.
 * 
 * Manages explicit transaction status transitions:
 * - PENDING -> ACTIVE -> COMMITTED
 * - PENDING -> ACTIVE -> ROLLED_BACK
 * - PENDING -> ACTIVE -> FAILED
 */
class LockManager;

class TransactionManager {
public:
    explicit TransactionManager(DatabaseManager& dbManager, LockManager* lockManager = nullptr);

    void setLockManager(LockManager* lockManager) noexcept { lockManager_ = lockManager; }
    LockManager* getLockManager() const noexcept { return lockManager_; }

    // Core Payment Operations (Business Logic)
    bool transfer(int senderAccountId, int receiverAccountId, double amount);
    bool transferFunds(int senderAccountId, int receiverAccountId, double amount) {
        return transfer(senderAccountId, receiverAccountId, amount);
    }
    bool deposit(int accountId, double amount);
    bool withdraw(int accountId, double amount);

    // Controlled Rollback Demonstration (Atomicity Proof for VIVA)
    bool demonstrateControlledRollback(int senderAccountId, int receiverAccountId, double amount);

    // Transaction Queries
    std::optional<Transaction> getTransaction(int transactionId);
    std::vector<Transaction> getTransactions();
    std::vector<Transaction> getTransactionsForAccount(int accountId);
    std::vector<Transaction> getAllTransactions() { return getTransactions(); }
    std::vector<Transaction> getTransactionHistory(int accountId = 0);
    std::vector<TransactionLog> getTransactionLogs(int transactionId = 0);

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
    LockManager* lockManager_{nullptr};
    mutable std::string lastError_;

    void setLastError(const std::string& err) const;
    static int64_t toCents(double amount);
    static std::string formatAmount(double amount);
    static int nextLogicalTxId();
};

} // namespace paylock

#endif // PAYLOCK_TRANSACTION_MANAGER_H
