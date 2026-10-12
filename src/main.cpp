#include "database/DatabaseManager.h"
#include "transaction/TransactionManager.h"
#include "concurrency/ConcurrentTransactionProcessor.h"
#include <iostream>
#include <iomanip>
#include <cassert>

void resetDatabaseState(paylock::DatabaseManager& db) {
    try {
        db.executeQuery("DELETE FROM transaction_logs;");
        db.executeQuery("DELETE FROM transactions;");
        db.executeQuery("ALTER TABLE transactions AUTO_INCREMENT = 1001;");
        db.executeQuery("ALTER TABLE transaction_logs AUTO_INCREMENT = 1;");
        db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
        db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");
        db.executeQuery("UPDATE accounts SET balance = 7000.00 WHERE account_id = 103;");
    } catch (const std::exception& ex) {
        std::cerr << "Warning during state reset: " << ex.what() << "\n";
    }
}

int main() {
    std::cout << "==================================================\n";
    std::cout << "        PAYLOCK CONCURRENT TRANSACTION SYSTEM     \n";
    std::cout << "   Day 3: OS Concurrency, Multithreading & Mutex  \n";
    std::cout << "==================================================\n\n";

    try {
        paylock::DatabaseConfig config = paylock::DatabaseConfig::fromEnvOrDefault();
        paylock::DatabaseManager adminDb(config);
        adminDb.connect();

        if (!adminDb.isConnected()) {
            std::cerr << "Failed to connect to MySQL database.\n";
            return 1;
        }
        std::cout << "[System]: Connected to MySQL database.\n\n";

        // ====================================================================
        // DEMO 1: CONTROLLED RACE CONDITION DEMONSTRATION (IN-MEMORY)
        // Demonstrates WHY synchronization is essential in concurrent systems.
        // ====================================================================
        std::cout << "==================================================\n";
        std::cout << "  PHASE 1: CONTROLLED RACE CONDITION DEMONSTRATION\n";
        std::cout << "==================================================\n";
        paylock::ConcurrentTransactionProcessor::demonstrateRaceCondition(10000.00, 7000.00, 6000.00);

        // ====================================================================
        // DEMO 2: CONCURRENT PAYMENT TRANSFERS ACROSS ACCOUNTS
        // Tests real multithreading with std::thread and dedicated connections.
        // ====================================================================
        std::cout << "==================================================\n";
        std::cout << "  PHASE 2: CONCURRENT PAYMENT PROCESSING (REAL DB)\n";
        std::cout << "==================================================\n\n";

        resetDatabaseState(adminDb);
        paylock::TransactionManager adminTx(adminDb);

        std::cout << "Initial Account Balances:\n";
        std::cout << "------------------------------------------------\n";
        auto initAccounts = adminTx.getAllAccounts();
        for (const auto& acc : initAccounts) {
            std::cout << "Account " << acc.accountId << " (" << acc.ownerName 
                      << ") : ₹" << std::fixed << std::setprecision(0) << acc.balance << "\n";
        }
        std::cout << "\n";

        paylock::ConcurrentTransactionProcessor processor(config);

        // Define 4 concurrent payment transfer requests
        std::vector<paylock::TransferRequest> transferRequests = {
            {"T1", 101, 102, 2000.00},
            {"T2", 101, 103, 3000.00},
            {"T3", 102, 103, 1000.00},
            {"T4", 103, 101,  500.00}
        };

        std::cout << "Launching 4 Concurrent Worker Threads:\n";
        std::cout << "  [T1] Account 101 -> Account 102 : ₹2,000\n";
        std::cout << "  [T2] Account 101 -> Account 103 : ₹3,000\n";
        std::cout << "  [T3] Account 102 -> Account 103 : ₹1,000\n";
        std::cout << "  [T4] Account 103 -> Account 101 : ₹500\n";
        std::cout << "------------------------------------------------\n";

        auto transferMetrics = processor.processConcurrentTransfers(transferRequests);

        std::cout << "\n";
        transferMetrics.print();

        // Programmatically calculate expected balances
        // A101: 10,000 - 2,000 - 3,000 + 500 = ₹5,500
        // A102:  5,000 + 2,000 - 1,000       = ₹6,000
        // A103:  7,000 + 3,000 + 1,000 - 500 = ₹10,500
        double expectedA101 = 10000.00 - 2000.00 - 3000.00 + 500.00;
        double expectedA102 =  5000.00 + 2000.00 - 1000.00;
        double expectedA103 =  7000.00 + 3000.00 + 1000.00 - 500.00;

        double actualA101 = adminTx.checkBalance(101).value_or(-1.0);
        double actualA102 = adminTx.checkBalance(102).value_or(-1.0);
        double actualA103 = adminTx.checkBalance(103).value_or(-1.0);

        std::cout << "Database Verification (Post-Concurrency):\n";
        std::cout << "------------------------------------------------\n";
        std::cout << "Account 101: Actual = ₹" << std::fixed << std::setprecision(0) << actualA101 
                  << " | Expected = ₹" << expectedA101 
                  << " -> " << (actualA101 == expectedA101 ? "MATCHED (PASS)" : "MISMATCH (FAIL)") << "\n";
        std::cout << "Account 102: Actual = ₹" << actualA102 
                  << " | Expected = ₹" << expectedA102 
                  << " -> " << (actualA102 == expectedA102 ? "MATCHED (PASS)" : "MISMATCH (FAIL)") << "\n";
        std::cout << "Account 103: Actual = ₹" << actualA103 
                  << " | Expected = ₹" << expectedA103 
                  << " -> " << (actualA103 == expectedA103 ? "MATCHED (PASS)" : "MISMATCH (FAIL)") << "\n\n";

        assert(actualA101 == expectedA101);
        assert(actualA102 == expectedA102);
        assert(actualA103 == expectedA103);

        // ====================================================================
        // DEMO 3: SAME-ACCOUNT CONCURRENT WITHDRAWAL CONTENTION
        // Tests multiple threads competing for the SAME shared account balance.
        // ====================================================================
        std::cout << "==================================================\n";
        std::cout << "  PHASE 3: SAME-ACCOUNT CONCURRENT CONTENTION TEST\n";
        std::cout << "==================================================\n\n";

        // Reset Account 101 to ₹10,000
        adminDb.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
        std::cout << "Initial Account 101 Balance: ₹10,000\n";
        std::cout << "Launching 3 Concurrent Withdrawals on Account 101:\n";
        std::cout << "  [T1] Withdraw ₹2,000\n";
        std::cout << "  [T2] Withdraw ₹3,000\n";
        std::cout << "  [T3] Withdraw ₹4,000\n";
        std::cout << "------------------------------------------------\n";

        std::vector<paylock::WithdrawalRequest> withdrawRequestsPartA = {
            {"T1", 101, 2000.00},
            {"T2", 101, 3000.00},
            {"T3", 101, 4000.00}
        };

        auto withdrawMetricsA = processor.processConcurrentWithdrawals(withdrawRequestsPartA);
        withdrawMetricsA.print();

        double actualBalPartA = adminTx.checkBalance(101).value_or(-1.0);
        double expectedBalPartA = 10000.00 - 2000.00 - 3000.00 - 4000.00; // ₹1,000
        std::cout << "Account 101 Final Balance: Actual = ₹" << actualBalPartA 
                  << " | Expected = ₹" << expectedBalPartA 
                  << " -> " << (actualBalPartA == expectedBalPartA ? "MATCHED (PASS)" : "MISMATCH (FAIL)") << "\n\n";
        assert(actualBalPartA == expectedBalPartA);

        // Part B: Overdraft contention on same account (T1=₹7,000, T2=₹6,000 on ₹10,000 balance)
        std::cout << "Testing Concurrent Overdraft Protection on Account 101:\n";
        std::cout << "Resetting Account 101 to ₹10,000\n";
        adminDb.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");

        std::cout << "Launching 2 Concurrent Competing Withdrawals:\n";
        std::cout << "  [T1] Withdraw ₹7,000\n";
        std::cout << "  [T2] Withdraw ₹6,000\n";
        std::cout << "  (Total ₹13,000 exceeds ₹10,000 balance -> Exactly 1 must succeed, 1 must fail)\n";
        std::cout << "------------------------------------------------\n";

        std::vector<paylock::WithdrawalRequest> withdrawRequestsPartB = {
            {"T1", 101, 7000.00},
            {"T2", 101, 6000.00}
        };

        auto withdrawMetricsB = processor.processConcurrentWithdrawals(withdrawRequestsPartB);
        withdrawMetricsB.print();

        double actualBalPartB = adminTx.checkBalance(101).value_or(-1.0);
        std::cout << "Account 101 Final Balance: ₹" << actualBalPartB << "\n";
        std::cout << "Successful: " << withdrawMetricsB.successfulTransactions 
                  << " | Failed: " << withdrawMetricsB.failedTransactions << "\n";
        bool overdraftPrevented = (withdrawMetricsB.successfulTransactions == 1 &&
                                   withdrawMetricsB.failedTransactions == 1 &&
                                   (actualBalPartB == 3000.00 || actualBalPartB == 4000.00));
        std::cout << "Overdraft Protection Verification: " 
                  << (overdraftPrevented ? "PASSED (Integrity Preserved)" : "FAILED") << "\n\n";
        assert(overdraftPrevented);

        // ====================================================================
        // TRANSACTION HISTORY & AUDIT TRAIL
        // ====================================================================
        adminTx.displayTransactionHistory();
        std::cout << "\n";

        // ====================================================================
        // SUMMARY: OS + DBMS INTEGRATION
        // ====================================================================
        std::cout << "==================================================\n";
        std::cout << "           OS + DBMS INTEGRATION REPORT           \n";
        std::cout << "==================================================\n";
        std::cout << "[OPERATING SYSTEMS]:\n"
                  << "  - Multithreading: Independent std::thread workers executing concurrently.\n"
                  << "  - Synchronization: Mutex (std::mutex, std::lock_guard) protects critical sections.\n"
                  << "  - Race Prevention: Serializes shared account modifications, preventing TOCTOU.\n"
                  << "  - Thread Lifecycle: Full state tracking (CREATED -> WAITING -> RUNNING -> COMPLETED).\n"
                  << "  - Thread-Safe Logging: Atomic console outputs via dedicated logging mutex.\n\n"
                  << "[DATABASE MANAGEMENT SYSTEMS]:\n"
                  << "  - Transaction Scopes: Dedicated per-thread MySQL connections/sessions.\n"
                  << "  - ACID Compliance: START TRANSACTION -> COMMIT / ROLLBACK per operation.\n"
                  << "  - Exact Arithmetic: DECIMAL(15, 2) preventing financial floating-point drift.\n"
                  << "  - Durability: Changes persisted to InnoDB redo log upon COMMIT.\n\n"
                  << "[KEY DISTINCTION FOR VIVA]:\n"
                  << "  - Mutex: Guards C++ in-memory execution and serializes shared account access.\n"
                  << "  - Database Transactions: Guarantees relational ACID atomicity and persistence.\n"
                  << "==================================================\n";

    } catch (const std::exception& ex) {
        std::cerr << "\n[System Error]: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
