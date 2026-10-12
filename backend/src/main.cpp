#include "database/DatabaseManager.h"
#include "transaction/TransactionManager.h"
#include "concurrency/ConcurrencyManager.h"
#include "concurrency/ConcurrentTransactionProcessor.h"
#include "logging/Logger.h"
#include "locking/LockManager.h"
#include "deadlock/DeadlockDetector.h"
#include "recovery/RecoveryManager.h"
#include "resources/ResourceManager.h"
#include <iostream>
#include <iomanip>
#include <cassert>
#include <string>
#include <vector>
#include <unistd.h>

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

void runControlledConcurrencyDemo(paylock::DatabaseManager& adminDb, const paylock::DatabaseConfig& config) {
    std::cout << "\n----------------------------------------\n";
    std::cout << "        PAYLOCK CONCURRENCY DEMO        \n";
    std::cout << "----------------------------------------\n\n";

    // Setup initial state:
    // A101 = ₹20,000, A102 = ₹5,000, A103 = ₹5,000
    adminDb.executeQuery("UPDATE accounts SET balance = 20000.00 WHERE account_id = 101;");
    adminDb.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");
    adminDb.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 103;");

    std::cout << "Initial Balance:\n";
    std::cout << "  A101 = ₹20,000\n";
    std::cout << "  A102 = ₹5,000\n";
    std::cout << "  A103 = ₹5,000\n\n";

    std::cout << "Concurrent Transactions:\n";
    std::cout << "  T001: A101 -> A102 ₹2,000\n";
    std::cout << "  T002: A101 -> A103 ₹3,000\n";
    std::cout << "  T003: A101 -> A102 ₹1,000\n\n";

    paylock::ConcurrencyManager cm(config);
    cm.start(3);

    std::cout << "Workers:\n";
    std::cout << "  W001 -> T001 -> RUNNING\n";
    std::cout << "  W002 -> T002 -> RUNNING\n";
    std::cout << "  W003 -> T003 -> RUNNING\n\n";

    cm.submitTransfer("T001", 101, 102, 2000.00);
    cm.submitTransfer("T002", 101, 103, 3000.00);
    cm.submitTransfer("T003", 101, 102, 1000.00);

    cm.waitForCompletion();
    cm.shutdown();

    std::cout << "After execution:\n";
    std::cout << "  T001 -> COMMITTED\n";
    std::cout << "  T002 -> COMMITTED\n";
    std::cout << "  T003 -> COMMITTED\n\n";

    auto b101 = adminDb.getAccountBalance(101).value_or(0.0);
    auto b102 = adminDb.getAccountBalance(102).value_or(0.0);
    auto b103 = adminDb.getAccountBalance(103).value_or(0.0);

    std::cout << "Final:\n";
    std::cout << "  A101 = ₹" << std::fixed << std::setprecision(0) << b101 << "\n";
    std::cout << "  A102 = ₹" << std::fixed << std::setprecision(0) << b102 << "\n";
    std::cout << "  A103 = ₹" << std::fixed << std::setprecision(0) << b103 << "\n";
    std::cout << "----------------------------------------\n\n";
}

void runAutomatedWalkthrough(paylock::DatabaseManager& adminDb, const paylock::DatabaseConfig& config) {
    std::cout << "\n==================================================\n";
    std::cout << "      PAYLOCK COMPLETE AUTOMATED SHOWCASE         \n";
    std::cout << "==================================================\n\n";

    paylock::TransactionManager txManager(adminDb);
    resetDatabaseState(adminDb);

    std::cout << "[Step 1] Initial Account State:\n";
    txManager.displayAccounts();

    std::cout << "\n[Step 2] Executing Valid Transfer: ₹2,000 from Account 101 -> Account 102\n";
    bool s1 = txManager.transfer(101, 102, 2000.00);
    std::cout << "Result: " << (s1 ? "SUCCESS (COMMITTED)" : "FAILED") << "\n";

    std::cout << "\n[Step 3] Executing Overdraft Transfer: ₹15,000 from Account 101 -> Account 103\n";
    bool s2 = txManager.transfer(101, 103, 15000.00);
    std::cout << "Result: " << (s2 ? "UNEXPECTED SUCCESS" : "SAFELY REJECTED & ROLLED BACK") << "\n";

    std::cout << "\n[Step 4] Executing Deposit: ₹1,500 into Account 102\n";
    bool s3 = txManager.deposit(102, 1500.00);
    std::cout << "Result: " << (s3 ? "SUCCESS (COMMITTED)" : "FAILED") << "\n";

    std::cout << "\n[Step 5] Executing Withdrawal: ₹2,500 from Account 103\n";
    bool s4 = txManager.withdraw(103, 2500.00);
    std::cout << "Result: " << (s4 ? "SUCCESS (COMMITTED)" : "FAILED") << "\n";

    std::cout << "\n[Step 6] Demonstrating Controlled MySQL Rollback (DBMS Atomicity):\n";
    txManager.demonstrateControlledRollback(101, 102, 1000.00);

    std::cout << "\n[Step 7] Current Account Balances:\n";
    txManager.displayAccounts();

    std::cout << "\n[Step 8] Shared Counter Race Condition & Critical Section Demo (OS Theory):\n";
    auto counterRes = paylock::ConcurrencyManager::demonstrateCounterRaceCondition(10, 10000);
    std::cout << "Result: Unsafe = " << counterRes.unsafeFinalCount 
              << " (Race occurred: " << (counterRes.raceConditionOccurred ? "YES" : "NO") 
              << ") | Safe Mutex = " << counterRes.safeFinalCount << " (Exact match)\n";

    std::cout << "\n[Step 9] Controlled Concurrency Demo (Multi-Worker Execution):\n";
    runControlledConcurrencyDemo(adminDb, config);

    std::cout << "\n==================================================\n";
    std::cout << "           OS + DBMS INTEGRATION SUMMARY          \n";
    std::cout << "==================================================\n";
    std::cout << "[OS Concepts]   : std::thread, std::mutex critical sections, worker state tracking.\n";
    std::cout << "[DBMS Concepts] : START TRANSACTION, COMMIT, ROLLBACK, ACID atomicity, WAL logs.\n";
    std::cout << "==================================================\n\n";
}

void runInteractiveCLI(paylock::DatabaseManager& adminDb, const paylock::DatabaseConfig& config) {
    paylock::TransactionManager txManager(adminDb);

    while (true) {
        std::cout << "\n========================================\n";
        std::cout << "                PAYLOCK                 \n";
        std::cout << "    Concurrent Transaction Engine       \n";
        std::cout << "========================================\n";
        std::cout << " 1. Transfer Funds (Single Transaction)\n";
        std::cout << " 2. Deposit Funds\n";
        std::cout << " 3. Withdraw Funds\n";
        std::cout << " 4. View Account Balances\n";
        std::cout << " 5. View Transaction History\n";
        std::cout << " 6. Controlled Rollback Demo (Atomicity)\n";
        std::cout << " 7. Controlled Concurrency Demo (Step 17)\n";
        std::cout << " 8. Shared Counter Race Condition Demo\n";
        std::cout << " 9. Run Full Automated Walkthrough\n";
        std::cout << "10. Reset Database to Seed State\n";
        std::cout << "11. Exit\n";
        std::cout << "----------------------------------------\n";
        std::cout << "Enter choice [1-11]: ";

        int choice = 0;
        if (!(std::cin >> choice)) {
            break;
        }

        switch (choice) {
            case 1: {
                int sender = 0, receiver = 0;
                double amount = 0.0;
                std::cout << "Enter Sender Account ID (e.g. 101): ";
                std::cin >> sender;
                std::cout << "Enter Receiver Account ID (e.g. 102): ";
                std::cin >> receiver;
                std::cout << "Enter Transfer Amount (₹): ";
                std::cin >> amount;
                bool ok = txManager.transfer(sender, receiver, amount);
                std::cout << "Result: " << (ok ? "SUCCESS (COMMITTED)" : ("FAILED: " + txManager.getLastError())) << "\n";
                break;
            }
            case 2: {
                int account = 0;
                double amount = 0.0;
                std::cout << "Enter Account ID (e.g. 101): ";
                std::cin >> account;
                std::cout << "Enter Deposit Amount (₹): ";
                std::cin >> amount;
                bool ok = txManager.deposit(account, amount);
                std::cout << "Result: " << (ok ? "SUCCESS (COMMITTED)" : ("FAILED: " + txManager.getLastError())) << "\n";
                break;
            }
            case 3: {
                int account = 0;
                double amount = 0.0;
                std::cout << "Enter Account ID (e.g. 101): ";
                std::cin >> account;
                std::cout << "Enter Withdrawal Amount (₹): ";
                std::cin >> amount;
                bool ok = txManager.withdraw(account, amount);
                std::cout << "Result: " << (ok ? "SUCCESS (COMMITTED)" : ("FAILED: " + txManager.getLastError())) << "\n";
                break;
            }
            case 4: {
                txManager.displayAccounts();
                break;
            }
            case 5: {
                txManager.displayTransactionHistory();
                break;
            }
            case 6: {
                int sender = 101, receiver = 102;
                double amount = 2000.0;
                std::cout << "Running Controlled Rollback Demonstration on Accounts 101 -> 102...\n";
                txManager.demonstrateControlledRollback(sender, receiver, amount);
                break;
            }
            case 7: {
                runControlledConcurrencyDemo(adminDb, config);
                break;
            }
            case 8: {
                std::cout << "Running Shared Counter Race Condition Demo (10 threads, 10,000 increments each)...\n";
                auto res = paylock::ConcurrencyManager::demonstrateCounterRaceCondition(10, 10000);
                std::cout << "\nResults:\n";
                std::cout << "Expected Count: " << res.expectedCount << "\n";
                std::cout << "Unsafe Count  : " << res.unsafeFinalCount << " (Lost " << (res.expectedCount - res.unsafeFinalCount) << " updates)\n";
                std::cout << "Safe Count    : " << res.safeFinalCount << " (Exact match guaranteed by std::mutex)\n";
                break;
            }
            case 9: {
                runAutomatedWalkthrough(adminDb, config);
                break;
            }
            case 10: {
                resetDatabaseState(adminDb);
                std::cout << "[System]: Database accounts and transactions reset to clean seed state.\n";
                txManager.displayAccounts();
                break;
            }
            case 11: {
                std::cout << "Exiting PayLock. Goodbye!\n";
                return;
            }
            default: {
                std::cout << "Invalid choice. Please enter a number between 1 and 11.\n";
                break;
            }
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        paylock::DatabaseConfig config = paylock::DatabaseConfig::fromEnvOrDefault();
        paylock::DatabaseManager adminDb(config);
        adminDb.connect();

        if (!adminDb.isConnected()) {
            std::cerr << "[CRITICAL ERROR]: Failed to connect to MySQL database.\n";
            return 1;
        }

        bool forceInteractive = false;
        bool forceAutomated = false;

        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "--interactive" || arg == "-i") {
                forceInteractive = true;
            } else if (arg == "--auto" || arg == "-a" || arg == "--demo") {
                forceAutomated = true;
            }
        }

        if (forceInteractive) {
            runInteractiveCLI(adminDb, config);
        } else if (forceAutomated || !isatty(STDIN_FILENO)) {
            runAutomatedWalkthrough(adminDb, config);
        } else {
            // Default interactive mode when running in a terminal
            runInteractiveCLI(adminDb, config);
        }

        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "[FATAL EXCEPTION]: " << ex.what() << "\n";
        return 1;
    }
}
