#include "transaction/TransactionManager.h"
#include "logging/Logger.h"
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
        Logger::warn("Validation failed: transfer amount must be strictly greater than 0 (₹" + formatAmount(amount) + ")", "TransactionManager");
        return false;
    }

    // 2. Validate Accounts are not identical
    if (senderAccountId == receiverAccountId) {
        setLastError("Sender and receiver accounts cannot be the same (" + std::to_string(senderAccountId) + ").");
        Logger::warn("Validation failed: identical sender and receiver account (" + std::to_string(senderAccountId) + ")", "TransactionManager");
        return false;
    }

    // 3. Validate Sender Account Exists
    auto senderAcc = db_.getAccountById(senderAccountId);
    if (!senderAcc.has_value()) {
        setLastError("Sender account " + std::to_string(senderAccountId) + " does not exist.");
        Logger::warn("Validation failed: Sender account " + std::to_string(senderAccountId) + " does not exist", "TransactionManager");
        try {
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
    Logger::info("Sender A" + std::to_string(senderAccountId) + " validated", "TransactionManager");

    // 4. Validate Receiver Account Exists
    auto receiverAcc = db_.getAccountById(receiverAccountId);
    if (!receiverAcc.has_value()) {
        setLastError("Receiver account " + std::to_string(receiverAccountId) + " does not exist.");
        Logger::warn("Validation failed: Receiver account " + std::to_string(receiverAccountId) + " does not exist", "TransactionManager");
        try {
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
    Logger::info("Receiver A" + std::to_string(receiverAccountId) + " validated", "TransactionManager");

    // 5. Validate Sender Balance (Pre-check & DBMS Atomicity Enforcement)
    if (toCents(senderAcc->balance) < toCents(amount)) {
        setLastError("Insufficient funds in Account " + std::to_string(senderAccountId) +
                     " (Available: ₹" + formatAmount(senderAcc->balance) +
                     ", Requested: ₹" + formatAmount(amount) + ")");
        Logger::warn("Insufficient balance in A" + std::to_string(senderAccountId) + 
                     " (Available: ₹" + formatAmount(senderAcc->balance) + 
                     ", Requested: ₹" + formatAmount(amount) + ")", "TransactionManager");

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
        Logger::warn("Transaction rolled back for A" + std::to_string(senderAccountId) + " due to insufficient funds", "TransactionManager");
        return false;
    }

    // 6. Execute Atomic Database Transaction (ACID Protected)
    try {
        db_.beginTransaction();

        // Step 6a: Insert transaction record in ACTIVE status
        int txId = db_.recordTransaction(senderAccountId, receiverAccountId, amount, "ACTIVE");
        if (txId <= 0) {
            throw DatabaseException("Failed to initialize transaction record.");
        }
        Logger::info("Transaction T" + std::to_string(txId) + " started [State: ACTIVE]", "TransactionManager");

        // Step 6b: Log TRANSFER_STARTED
        db_.recordTransactionLog(txId, "TRANSFER_STARTED",
            "Sender: " + std::to_string(senderAccountId) +
            "\nReceiver: " + std::to_string(receiverAccountId) +
            "\nAmount: ₹" + formatAmount(amount));

        // Step 6c: Debit sender balance
        if (!db_.debitAccount(senderAccountId, amount)) {
            throw DatabaseException("Failed to debit sender account " + std::to_string(senderAccountId));
        }
        Logger::info("₹" + formatAmount(amount) + " deducted from A" + std::to_string(senderAccountId), "TransactionManager");
        db_.recordTransactionLog(txId, "SENDER_BALANCE_DEBITED",
            "Debited ₹" + formatAmount(amount) + " from Account " + std::to_string(senderAccountId));

        // Step 6d: Credit receiver balance
        if (!db_.creditAccount(receiverAccountId, amount)) {
            throw DatabaseException("Failed to credit receiver account " + std::to_string(receiverAccountId));
        }
        Logger::info("₹" + formatAmount(amount) + " added to A" + std::to_string(receiverAccountId), "TransactionManager");
        db_.recordTransactionLog(txId, "RECEIVER_BALANCE_CREDITED",
            "Credited ₹" + formatAmount(amount) + " to Account " + std::to_string(receiverAccountId));

        // Step 6e: Mark transaction as COMMITTED
        db_.updateTransactionStatus(txId, "COMMITTED");
        db_.recordTransactionLog(txId, "TRANSACTION_COMMITTED",
            "Transfer completed and permanently committed to MySQL.");
        Logger::info("Transaction T" + std::to_string(txId) + " committed [State: COMMITTED]", "TransactionManager");

        // Step 6f: Commit MySQL transaction (Durability)
        db_.commit();
        return true;

    } catch (const std::exception& ex) {
        // Guarantee Atomicity by rolling back all intermediate changes
        try {
            db_.rollback();
            Logger::warn("Transaction rolled back due to error: " + std::string(ex.what()), "TransactionManager");
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
        Logger::warn("Validation failed: deposit amount must be positive (₹" + formatAmount(amount) + ")", "TransactionManager");
        return false;
    }

    if (!db_.accountExists(accountId)) {
        setLastError("Account " + std::to_string(accountId) + " does not exist.");
        Logger::warn("Validation failed: Account " + std::to_string(accountId) + " does not exist for deposit", "TransactionManager");
        return false;
    }

    Logger::info("Account A" + std::to_string(accountId) + " validated for deposit", "TransactionManager");

    try {
        db_.beginTransaction();

        // 0 indicates external source (cash/ATM/system)
        int txId = db_.recordTransaction(0, accountId, amount, "ACTIVE");
        if (txId <= 0) {
            throw DatabaseException("Failed to record deposit transaction.");
        }
        Logger::info("Deposit transaction T" + std::to_string(txId) + " started [State: ACTIVE]", "TransactionManager");

        db_.recordTransactionLog(txId, "DEPOSIT_STARTED",
            "Deposit of ₹" + formatAmount(amount) + " into Account " + std::to_string(accountId));

        if (!db_.creditAccount(accountId, amount)) {
            throw DatabaseException("Failed to credit deposit to account " + std::to_string(accountId));
        }
        Logger::info("₹" + formatAmount(amount) + " deposited into A" + std::to_string(accountId), "TransactionManager");
        db_.recordTransactionLog(txId, "RECEIVER_BALANCE_CREDITED",
            "Credited ₹" + formatAmount(amount) + " to Account " + std::to_string(accountId));

        db_.updateTransactionStatus(txId, "COMMITTED");
        db_.recordTransactionLog(txId, "TRANSACTION_COMMITTED",
            "Deposit committed successfully.");
        Logger::info("Deposit transaction T" + std::to_string(txId) + " committed [State: COMMITTED]", "TransactionManager");

        db_.commit();
        return true;

    } catch (const std::exception& ex) {
        try {
            db_.rollback();
            Logger::warn("Deposit rolled back due to error: " + std::string(ex.what()), "TransactionManager");
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
        Logger::warn("Validation failed: withdrawal amount must be positive (₹" + formatAmount(amount) + ")", "TransactionManager");
        return false;
    }

    auto acc = db_.getAccountById(accountId);
    if (!acc.has_value()) {
        setLastError("Account " + std::to_string(accountId) + " does not exist.");
        Logger::warn("Validation failed: Account " + std::to_string(accountId) + " does not exist for withdrawal", "TransactionManager");
        return false;
    }

    // Check available balance
    if (toCents(acc->balance) < toCents(amount)) {
        setLastError("Insufficient funds in Account " + std::to_string(accountId) +
                     " (Available: ₹" + formatAmount(acc->balance) +
                     ", Requested: ₹" + formatAmount(amount) + ")");
        Logger::warn("Withdrawal failed: insufficient balance in A" + std::to_string(accountId) + 
                     " (Available: ₹" + formatAmount(acc->balance) + 
                     ", Requested: ₹" + formatAmount(amount) + ")", "TransactionManager");

        try {
            db_.beginTransaction();
            db_.rollback();
        } catch (...) {}

        // 0 indicates external sink (cash withdrawal)
        int txId = db_.recordTransaction(accountId, 0, amount, "FAILED");
        if (txId > 0) {
            db_.recordTransactionLog(txId, "WITHDRAWAL_STARTED",
                "Withdrawal request of ₹" + formatAmount(amount) + " from Account " + std::to_string(accountId));
            db_.recordTransactionLog(txId, "WITHDRAWAL_FAILED", lastError_);
            db_.recordTransactionLog(txId, "TRANSACTION_FAILED",
                "Withdrawal failed due to insufficient balance. Balance unchanged.");
        }
        return false;
    }

    Logger::info("Account A" + std::to_string(accountId) + " validated for withdrawal", "TransactionManager");

    try {
        db_.beginTransaction();

        int txId = db_.recordTransaction(accountId, 0, amount, "ACTIVE");
        if (txId <= 0) {
            throw DatabaseException("Failed to record withdrawal transaction.");
        }
        Logger::info("Withdrawal transaction T" + std::to_string(txId) + " started [State: ACTIVE]", "TransactionManager");

        db_.recordTransactionLog(txId, "WITHDRAWAL_STARTED",
            "Withdrawal of ₹" + formatAmount(amount) + " from Account " + std::to_string(accountId));

        if (!db_.debitAccount(accountId, amount)) {
            throw DatabaseException("Failed to debit withdrawal from account " + std::to_string(accountId));
        }
        Logger::info("₹" + formatAmount(amount) + " deducted from A" + std::to_string(accountId), "TransactionManager");
        db_.recordTransactionLog(txId, "SENDER_BALANCE_DEBITED",
            "Debited ₹" + formatAmount(amount) + " from Account " + std::to_string(accountId));

        db_.updateTransactionStatus(txId, "COMMITTED");
        db_.recordTransactionLog(txId, "TRANSACTION_COMMITTED",
            "Withdrawal committed successfully.");
        Logger::info("Withdrawal transaction T" + std::to_string(txId) + " committed [State: COMMITTED]", "TransactionManager");

        db_.commit();
        return true;

    } catch (const std::exception& ex) {
        try {
            db_.rollback();
            Logger::warn("Withdrawal rolled back due to error: " + std::string(ex.what()), "TransactionManager");
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
// Controlled Rollback Demonstration (Atomicity Proof for VIVA)
// ----------------------------------------------------------------------------

bool TransactionManager::demonstrateControlledRollback(int senderAccountId, int receiverAccountId, double amount) {
    Logger::info("=== STARTING CONTROLLED ROLLBACK ATOMICITY DEMONSTRATION ===", "TransactionManager");

    auto preSender = db_.getAccountById(senderAccountId);
    auto preReceiver = db_.getAccountById(receiverAccountId);

    if (!preSender || !preReceiver) {
        setLastError("Invalid accounts for rollback demo.");
        Logger::error("Invalid accounts provided for rollback demonstration.", "TransactionManager");
        return false;
    }

    double initSenderBalance = preSender->balance;
    double initReceiverBalance = preReceiver->balance;

    Logger::info("Initial State: Account " + std::to_string(senderAccountId) + " = ₹" + formatAmount(initSenderBalance) +
                 " | Account " + std::to_string(receiverAccountId) + " = ₹" + formatAmount(initReceiverBalance), "TransactionManager");
    Logger::info("Attempting transfer of ₹" + formatAmount(amount) + " with an intentional mid-transaction failure...", "TransactionManager");

    try {
        // Step 1: START TRANSACTION
        db_.beginTransaction();
        Logger::info("[STEP 1] MySQL START TRANSACTION executed.", "TransactionManager");

        int txId = db_.recordTransaction(senderAccountId, receiverAccountId, amount, "ACTIVE");
        Logger::info("[STEP 2] Transaction record created in ACTIVE state (Tx #" + std::to_string(txId) + ").", "TransactionManager");

        // Step 3: Debit sender
        db_.debitAccount(senderAccountId, amount);
        Logger::info("[STEP 3] Debited ₹" + formatAmount(amount) + " from Account " + std::to_string(senderAccountId) + 
                     " (Temporary in-flight balance: ₹" + formatAmount(initSenderBalance - amount) + ").", "TransactionManager");

        // Step 4: Deliberately simulate catastrophic failure before credit/commit
        Logger::warn("[STEP 4] SIMULATING UNEXPECTED HOST CRASH / NETWORK FAILURE BEFORE RECEIVER CREDIT!", "TransactionManager");
        throw std::runtime_error("Simulated catastrophic crash before transaction commit.");

    } catch (const std::exception& ex) {
        Logger::warn("[STEP 5] Exception caught: " + std::string(ex.what()), "TransactionManager");
        
        // Step 6: Issue ROLLBACK
        db_.rollback();
        Logger::info("[STEP 6] MySQL ROLLBACK executed. Discarding uncommitted buffer changes.", "TransactionManager");
    }

    // Step 7: Verify final balances in MySQL
    auto postSender = db_.getAccountById(senderAccountId);
    auto postReceiver = db_.getAccountById(receiverAccountId);

    if (!postSender || !postReceiver) {
        return false;
    }

    bool senderUntouched = (toCents(postSender->balance) == toCents(initSenderBalance));
    bool receiverUntouched = (toCents(postReceiver->balance) == toCents(initReceiverBalance));

    Logger::info("Post-Rollback State: Account " + std::to_string(senderAccountId) + " = ₹" + formatAmount(postSender->balance) +
                 " | Account " + std::to_string(receiverAccountId) + " = ₹" + formatAmount(postReceiver->balance), "TransactionManager");

    if (senderUntouched && receiverUntouched) {
        Logger::info(">>> ATOMICITY VERIFIED: Balances remain 100% untouched despite mid-flight failure!", "TransactionManager");
        return true;
    } else {
        Logger::error(">>> ATOMICITY VIOLATION: Balances were corrupted!", "TransactionManager");
        return false;
    }
}

// ----------------------------------------------------------------------------
// Transaction & Account Query Operations
// ----------------------------------------------------------------------------

std::optional<Transaction> TransactionManager::getTransaction(int transactionId) {
    return db_.getTransactionById(transactionId);
}

std::vector<Transaction> TransactionManager::getTransactions() {
    return db_.listTransactions();
}

std::vector<Transaction> TransactionManager::getTransactionsForAccount(int accountId) {
    if (accountId <= 0) {
        return db_.listTransactions();
    }
    return db_.getTransactionsForAccount(accountId);
}

std::vector<Transaction> TransactionManager::getTransactionHistory(int accountId) {
    return getTransactionsForAccount(accountId);
}

std::vector<TransactionLog> TransactionManager::getTransactionLogs(int transactionId) {
    return db_.listTransactionLogs(transactionId);
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
