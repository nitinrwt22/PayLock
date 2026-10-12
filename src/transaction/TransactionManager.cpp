#include "TransactionManager.h"
#include <iostream>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace paylock {

TransactionManager::TransactionManager(DatabaseManager& dbManager)
    : db_(dbManager) {}

int64_t TransactionManager::toCents(double amount) {
    return std::llround(amount * 100.0);
}

std::string TransactionManager::formatAmount(double amount) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << amount;
    return ss.str();
}

void TransactionManager::setLastError(const std::string& err) const {
    lastError_ = err;
}

std::string TransactionManager::getLastError() const {
    return lastError_;
}

// ----------------------------------------------------------------------------
// Core Business Operations: Transfer, Deposit, Withdraw
// ----------------------------------------------------------------------------

bool TransactionManager::transfer(int senderAccountId, int receiverAccountId, double amount) {
    // 1. Validate Amount
    if (amount <= 0.0) {
        setLastError("Transfer amount must be strictly greater than 0.");
        return false;
    }

    // 2. Validate Accounts are not identical
    if (senderAccountId == receiverAccountId) {
        setLastError("Sender and receiver accounts cannot be the same (" + std::to_string(senderAccountId) + ").");
        return false;
    }

    // 3. Validate Sender Account Exists
    auto senderAcc = db_.getAccountById(senderAccountId);
    if (!senderAcc.has_value()) {
        setLastError("Sender account " + std::to_string(senderAccountId) + " does not exist.");
        try {
            // Use 0 (NULL in DB) for invalid account so foreign key constraint is satisfied
            int txId = db_.recordTransaction(0, receiverAccountId, amount, "ROLLED_BACK");
            if (txId > 0) {
                db_.recordTransactionLog(txId, "TRANSFER_STARTED", 
                    "Attempted transfer of ₹" + formatAmount(amount) + " from non-existent Account " + std::to_string(senderAccountId));
                db_.recordTransactionLog(txId, "TRANSFER_FAILED", lastError_);
                db_.recordTransactionLog(txId, "TRANSACTION_ROLLED_BACK", "Transaction aborted due to non-existent sender account.");
            }
        } catch (...) {}
        return false;
    }

    // 4. Validate Receiver Account Exists
    auto receiverAcc = db_.getAccountById(receiverAccountId);
    if (!receiverAcc.has_value()) {
        setLastError("Receiver account " + std::to_string(receiverAccountId) + " does not exist.");
        try {
            // Use 0 (NULL in DB) for invalid account so foreign key constraint is satisfied
            int txId = db_.recordTransaction(senderAccountId, 0, amount, "ROLLED_BACK");
            if (txId > 0) {
                db_.recordTransactionLog(txId, "TRANSFER_STARTED", 
                    "Initiating transfer of ₹" + formatAmount(amount) + " from Account " + std::to_string(senderAccountId) + " to non-existent Account " + std::to_string(receiverAccountId));
                db_.recordTransactionLog(txId, "TRANSFER_FAILED", lastError_);
                db_.recordTransactionLog(txId, "TRANSACTION_ROLLED_BACK", "Transaction aborted due to non-existent receiver account.");
            }
        } catch (...) {}
        return false;
    }

    // 5. Validate Sender Balance (Pre-check & DBMS Atomicity Demonstration)
    if (toCents(senderAcc->balance) < toCents(amount)) {
        setLastError("Insufficient funds in Account " + std::to_string(senderAccountId) +
                     " (Available: ₹" + formatAmount(senderAcc->balance) +
                     ", Requested: ₹" + formatAmount(amount) + ")");

        // Explicitly demonstrate START TRANSACTION -> Abort -> ROLLBACK lifecycle
        try {
            db_.beginTransaction();
            db_.rollback();
        } catch (...) {}

        // Persist failure audit trail
        int txId = db_.recordTransaction(senderAccountId, receiverAccountId, amount, "ROLLED_BACK");
        if (txId > 0) {
            db_.recordTransactionLog(txId, "TRANSFER_STARTED", 
                "Sender: " + std::to_string(senderAccountId) + 
                "\nReceiver: " + std::to_string(receiverAccountId) + 
                "\nAmount: ₹" + formatAmount(amount));
            db_.recordTransactionLog(txId, "TRANSFER_FAILED", lastError_);
            db_.recordTransactionLog(txId, "TRANSACTION_ROLLED_BACK", 
                "Transaction rolled back due to insufficient sender balance. Balances unchanged.");
        }
        return false;
    }

    // 6. Execute Atomic Database Transaction (ACID Protected)
    try {
        db_.beginTransaction();

        // Step 6a: Insert transaction record in PROCESSING status
        int txId = db_.recordTransaction(senderAccountId, receiverAccountId, amount, "PROCESSING");
        if (txId <= 0) {
            throw DatabaseException("Failed to initialize transaction record.");
        }

        // Step 6b: Log TRANSFER_STARTED
        db_.recordTransactionLog(txId, "TRANSFER_STARTED",
            "Sender: " + std::to_string(senderAccountId) +
            "\nReceiver: " + std::to_string(receiverAccountId) +
            "\nAmount: ₹" + formatAmount(amount));

        // Step 6c: Debit sender balance
        if (!db_.debitAccount(senderAccountId, amount)) {
            throw DatabaseException("Failed to debit sender account " + std::to_string(senderAccountId));
        }
        db_.recordTransactionLog(txId, "SENDER_BALANCE_DEBITED",
            "Debited ₹" + formatAmount(amount) + " from Account " + std::to_string(senderAccountId));

        // Step 6d: Credit receiver balance
        if (!db_.creditAccount(receiverAccountId, amount)) {
            throw DatabaseException("Failed to credit receiver account " + std::to_string(receiverAccountId));
        }
        db_.recordTransactionLog(txId, "RECEIVER_BALANCE_CREDITED",
            "Credited ₹" + formatAmount(amount) + " to Account " + std::to_string(receiverAccountId));

        // Step 6e: Mark transaction as COMMITTED
        db_.updateTransactionStatus(txId, "COMMITTED");
        db_.recordTransactionLog(txId, "TRANSACTION_COMMITTED",
            "Transfer completed and permanently committed to MySQL.");

        // Step 6f: Commit MySQL transaction (Durability)
        db_.commit();
        return true;

    } catch (const std::exception& ex) {
        // Guarantee Atomicity by rolling back all intermediate changes
        try {
            db_.rollback();
        } catch (...) {}

        setLastError(std::string("Transaction error encountered: ") + ex.what());

        // Autonomous failure logging for audit trail
        try {
            int failedTxId = db_.recordTransaction(senderAccountId, receiverAccountId, amount, "ROLLED_BACK");
            if (failedTxId > 0) {
                db_.recordTransactionLog(failedTxId, "TRANSFER_STARTED",
                    "Sender: " + std::to_string(senderAccountId) +
                    "\nReceiver: " + std::to_string(receiverAccountId) +
                    "\nAmount: ₹" + formatAmount(amount));
                db_.recordTransactionLog(failedTxId, "TRANSFER_FAILED", ex.what());
                db_.recordTransactionLog(failedTxId, "TRANSACTION_ROLLED_BACK",
                    "Database rollback executed. No accounts were debited or credited.");
            }
        } catch (...) {}

        return false;
    }
}

bool TransactionManager::deposit(int accountId, double amount) {
    if (amount <= 0.0) {
        setLastError("Deposit amount must be strictly positive.");
        return false;
    }

    if (!db_.accountExists(accountId)) {
        setLastError("Account " + std::to_string(accountId) + " does not exist.");
        return false;
    }

    try {
        db_.beginTransaction();

        // 0 indicates external source (cash/ATM/system)
        int txId = db_.recordTransaction(0, accountId, amount, "PROCESSING");
        if (txId <= 0) {
            throw DatabaseException("Failed to record deposit transaction.");
        }

        db_.recordTransactionLog(txId, "DEPOSIT_STARTED",
            "Deposit of ₹" + formatAmount(amount) + " into Account " + std::to_string(accountId));

        if (!db_.creditAccount(accountId, amount)) {
            throw DatabaseException("Failed to credit deposit to account " + std::to_string(accountId));
        }
        db_.recordTransactionLog(txId, "RECEIVER_BALANCE_CREDITED",
            "Credited ₹" + formatAmount(amount) + " to Account " + std::to_string(accountId));

        db_.updateTransactionStatus(txId, "COMMITTED");
        db_.recordTransactionLog(txId, "TRANSACTION_COMMITTED",
            "Deposit committed successfully.");

        db_.commit();
        return true;

    } catch (const std::exception& ex) {
        try {
            db_.rollback();
        } catch (...) {}

        setLastError(std::string("Deposit failed: ") + ex.what());
        try {
            int failedTxId = db_.recordTransaction(0, accountId, amount, "ROLLED_BACK");
            if (failedTxId > 0) {
                db_.recordTransactionLog(failedTxId, "DEPOSIT_STARTED", "Deposit initiated.");
                db_.recordTransactionLog(failedTxId, "DEPOSIT_FAILED", ex.what());
                db_.recordTransactionLog(failedTxId, "TRANSACTION_ROLLED_BACK", "Database rolled back.");
            }
        } catch (...) {}

        return false;
    }
}

bool TransactionManager::withdraw(int accountId, double amount) {
    if (amount <= 0.0) {
        setLastError("Withdrawal amount must be strictly positive.");
        return false;
    }

    auto acc = db_.getAccountById(accountId);
    if (!acc.has_value()) {
        setLastError("Account " + std::to_string(accountId) + " does not exist.");
        return false;
    }

    // Check available balance
    if (toCents(acc->balance) < toCents(amount)) {
        setLastError("Insufficient funds in Account " + std::to_string(accountId) +
                     " (Available: ₹" + formatAmount(acc->balance) +
                     ", Requested: ₹" + formatAmount(amount) + ")");

        try {
            db_.beginTransaction();
            db_.rollback();
        } catch (...) {}

        // 0 indicates external sink (cash withdrawal)
        int txId = db_.recordTransaction(accountId, 0, amount, "ROLLED_BACK");
        if (txId > 0) {
            db_.recordTransactionLog(txId, "WITHDRAWAL_STARTED",
                "Withdrawal request of ₹" + formatAmount(amount) + " from Account " + std::to_string(accountId));
            db_.recordTransactionLog(txId, "WITHDRAWAL_FAILED", lastError_);
            db_.recordTransactionLog(txId, "TRANSACTION_ROLLED_BACK",
                "Transaction rolled back due to insufficient balance. Balance unchanged.");
        }
        return false;
    }

    try {
        db_.beginTransaction();

        int txId = db_.recordTransaction(accountId, 0, amount, "PROCESSING");
        if (txId <= 0) {
            throw DatabaseException("Failed to record withdrawal transaction.");
        }

        db_.recordTransactionLog(txId, "WITHDRAWAL_STARTED",
            "Withdrawal of ₹" + formatAmount(amount) + " from Account " + std::to_string(accountId));

        if (!db_.debitAccount(accountId, amount)) {
            throw DatabaseException("Failed to debit withdrawal from account " + std::to_string(accountId));
        }
        db_.recordTransactionLog(txId, "SENDER_BALANCE_DEBITED",
            "Debited ₹" + formatAmount(amount) + " from Account " + std::to_string(accountId));

        db_.updateTransactionStatus(txId, "COMMITTED");
        db_.recordTransactionLog(txId, "TRANSACTION_COMMITTED",
            "Withdrawal committed successfully.");

        db_.commit();
        return true;

    } catch (const std::exception& ex) {
        try {
            db_.rollback();
        } catch (...) {}

        setLastError(std::string("Withdrawal failed: ") + ex.what());
        try {
            int failedTxId = db_.recordTransaction(accountId, 0, amount, "ROLLED_BACK");
            if (failedTxId > 0) {
                db_.recordTransactionLog(failedTxId, "WITHDRAWAL_STARTED", "Withdrawal initiated.");
                db_.recordTransactionLog(failedTxId, "WITHDRAWAL_FAILED", ex.what());
                db_.recordTransactionLog(failedTxId, "TRANSACTION_ROLLED_BACK", "Database rolled back.");
            }
        } catch (...) {}

        return false;
    }
}

// ----------------------------------------------------------------------------
// Transaction & Account Query Operations
// ----------------------------------------------------------------------------

std::optional<Transaction> TransactionManager::getTransaction(int transactionId) {
    return db_.getTransactionById(transactionId);
}

std::vector<Transaction> TransactionManager::getTransactionHistory(int accountId) {
    auto allTx = db_.listTransactions();
    if (accountId <= 0) return allTx;

    std::vector<Transaction> filtered;
    for (const auto& tx : allTx) {
        if (tx.senderAccount == accountId || tx.receiverAccount == accountId) {
            filtered.push_back(tx);
        }
    }
    return filtered;
}

std::vector<TransactionLog> TransactionManager::getTransactionLogs(int transactionId) {
    return db_.listTransactionLogs(transactionId);
}

std::vector<Transaction> TransactionManager::getAllTransactions() {
    return db_.listTransactions();
}

std::optional<Account> TransactionManager::getAccount(int accountId) {
    return db_.getAccountById(accountId);
}

std::optional<double> TransactionManager::checkBalance(int accountId) {
    return db_.getAccountBalance(accountId);
}

std::vector<Account> TransactionManager::getAllAccounts() {
    return db_.listAccounts();
}

// ----------------------------------------------------------------------------
// Visual Display Functions
// ----------------------------------------------------------------------------

void TransactionManager::displayTransactionHistory() const {
    auto txs = db_.listTransactions();

    std::cout << "========================================\n";
    std::cout << "          PAYLOCK TRANSACTIONS\n";
    std::cout << "========================================\n\n";

    std::cout << std::left 
              << std::setw(7)  << "ID"
              << std::setw(8)  << "FROM"
              << std::setw(8)  << "TO"
              << std::setw(13) << "AMOUNT"
              << "STATUS\n";
    std::cout << "------------------------------------------------\n";

    for (const auto& tx : txs) {
        std::ostringstream amtStr;
        amtStr << "₹" << std::fixed << std::setprecision(0) << tx.amount;

        std::cout << std::left
                  << std::setw(7)  << tx.transactionId
                  << std::setw(8)  << tx.senderAccount
                  << std::setw(8)  << tx.receiverAccount
                  << std::setw(13) << amtStr.str()
                  << tx.status << "\n";
    }

    std::cout << "\n========================================\n";
}

void TransactionManager::displayAccounts() const {
    auto accounts = db_.listAccounts();
    std::cout << "========================================\n";
    std::cout << "            ACCOUNT BALANCES\n";
    std::cout << "========================================\n";
    for (const auto& acc : accounts) {
        std::cout << "Account " << std::left << std::setw(3) << acc.accountId
                  << " (" << std::setw(6) << acc.ownerName << ")"
                  << " : ₹" << std::fixed << std::setprecision(2) << acc.balance << "\n";
    }
    std::cout << "========================================\n";
}

void TransactionManager::displayTransactionAudit(int transactionId) const {
    auto logs = db_.listTransactionLogs(transactionId);
    if (logs.empty()) {
        std::cout << "No logs found for Transaction #" << transactionId << "\n";
        return;
    }

    auto tx = db_.getTransactionById(transactionId);
    std::cout << "Transaction #" << transactionId << "\n";
    std::cout << "-------------------------------\n";
    for (const auto& log : logs) {
        std::cout << log.operation << "\n";
        if (!log.message.empty()) {
            std::cout << log.message << "\n";
        }
        std::cout << "\n";
    }
}

} // namespace paylock
