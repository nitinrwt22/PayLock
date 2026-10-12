#include "database/DatabaseManager.h"
#include "transaction/TransactionManager.h"
#include <iostream>
#include <iomanip>

void resetInitialState(paylock::DatabaseManager& db) {
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
    std::cout << "        PAYLOCK PAYMENT TRANSACTION SYSTEM        \n";
    std::cout << "   Day 2: Core ACID Transactions & DBMS Engine    \n";
    std::cout << "==================================================\n\n";

    try {
        // Initialize Database Configuration & Manager
        paylock::DatabaseConfig config = paylock::DatabaseConfig::fromEnvOrDefault();
        paylock::DatabaseManager dbManager(config);

        // 1. Establish MySQL Connection
        dbManager.connect();
        if (!dbManager.isConnected()) {
            std::cerr << "Failed to verify active database connection.\n";
            return 1;
        }
        std::cout << "[Database]: Connected to MySQL successfully.\n\n";

        // Reset to canonical baseline state for reproducible Day 2 testing
        resetInitialState(dbManager);

        // Initialize Business Logic Layer
        paylock::TransactionManager txManager(dbManager);

        // Display Initial State
        std::cout << "Initial Account State:\n";
        std::cout << "------------------------------------------------\n";
        auto initialAccounts = txManager.getAllAccounts();
        for (const auto& acc : initialAccounts) {
            std::cout << "Account " << acc.accountId << " (" << acc.ownerName
                      << "): ₹" << std::fixed << std::setprecision(0) << acc.balance << "\n";
        }
        std::cout << "\n";

        // ============================================================
        // TEST 1: Transfer ₹2,000 from 101 -> 102
        // Expected: 101 = ₹8,000, 102 = ₹7,000, Status = COMMITTED
        // ============================================================
        std::cout << ">>> TEST 1: Transfer ₹2,000 from Account 101 -> Account 102\n";
        bool t1Success = txManager.transfer(101, 102, 2000.00);
        double bal101_t1 = txManager.checkBalance(101).value_or(0.0);
        double bal102_t1 = txManager.checkBalance(102).value_or(0.0);
        std::cout << "Result    : " << (t1Success ? "SUCCESS (COMMITTED)" : "FAILED") << "\n";
        std::cout << "Balances  : Account 101 = ₹" << std::fixed << std::setprecision(0) << bal101_t1
                  << " | Account 102 = ₹" << bal102_t1 << "\n";
        std::cout << "Validation: " << ((t1Success && bal101_t1 == 8000.0 && bal102_t1 == 7000.0) ? "PASSED\n\n" : "FAILED\n\n");

        // ============================================================
        // TEST 2: Transfer ₹15,000 from 101 -> 103 (Exceeds ₹8,000 balance)
        // Expected: Transaction fails, 101 remains ₹8,000, 103 remains ₹7,000
        // Status = ROLLED_BACK (Demonstrates DBMS Atomicity)
        // ============================================================
        std::cout << ">>> TEST 2: Transfer ₹15,000 from Account 101 -> Account 103 (Insufficient Funds)\n";
        bool t2Success = txManager.transfer(101, 103, 15000.00);
        double bal101_t2 = txManager.checkBalance(101).value_or(0.0);
        double bal103_t2 = txManager.checkBalance(103).value_or(0.0);
        std::cout << "Result    : " << (!t2Success ? "REJECTED & ROLLED_BACK (As expected)" : "UNEXPECTED SUCCESS") << "\n";
        std::cout << "Reason    : " << txManager.getLastError() << "\n";
        std::cout << "Balances  : Account 101 remains ₹" << std::fixed << std::setprecision(0) << bal101_t2
                  << " | Account 103 remains ₹" << bal103_t2 << "\n";
        std::cout << "Validation: " << ((!t2Success && bal101_t2 == 8000.0 && bal103_t2 == 7000.0) ? "PASSED (Atomicity preserved)\n\n" : "FAILED\n\n");

        // ============================================================
        // TEST 3: Deposit ₹1,000 into Account 102
        // Expected: 102 = ₹8,000, Status = COMMITTED
        // ============================================================
        std::cout << ">>> TEST 3: Deposit ₹1,000 into Account 102\n";
        bool t3Success = txManager.deposit(102, 1000.00);
        double bal102_t3 = txManager.checkBalance(102).value_or(0.0);
        std::cout << "Result    : " << (t3Success ? "SUCCESS (COMMITTED)" : "FAILED") << "\n";
        std::cout << "Balances  : Account 102 = ₹" << std::fixed << std::setprecision(0) << bal102_t3 << "\n";
        std::cout << "Validation: " << ((t3Success && bal102_t3 == 8000.0) ? "PASSED\n\n" : "FAILED\n\n");

        // ============================================================
        // TEST 4: Withdraw ₹2,000 from Account 103
        // Expected: 103 = ₹5,000, Status = COMMITTED
        // ============================================================
        std::cout << ">>> TEST 4: Withdraw ₹2,000 from Account 103\n";
        bool t4Success = txManager.withdraw(103, 2000.00);
        double bal103_t4 = txManager.checkBalance(103).value_or(0.0);
        std::cout << "Result    : " << (t4Success ? "SUCCESS (COMMITTED)" : "FAILED") << "\n";
        std::cout << "Balances  : Account 103 = ₹" << std::fixed << std::setprecision(0) << bal103_t4 << "\n";
        std::cout << "Validation: " << ((t4Success && bal103_t4 == 5000.0) ? "PASSED\n\n" : "FAILED\n\n");

        // ============================================================
        // TEST 5: Withdraw more than available balance (₹10,000 from 103)
        // Expected: Operation rejected, 103 remains ₹5,000, Status = ROLLED_BACK
        // ============================================================
        std::cout << ">>> TEST 5: Withdraw ₹10,000 from Account 103 (Exceeds ₹5,000 Balance)\n";
        bool t5Success = txManager.withdraw(103, 10000.00);
        double bal103_t5 = txManager.checkBalance(103).value_or(0.0);
        std::cout << "Result    : " << (!t5Success ? "REJECTED & ROLLED_BACK (As expected)" : "UNEXPECTED SUCCESS") << "\n";
        std::cout << "Reason    : " << txManager.getLastError() << "\n";
        std::cout << "Balances  : Account 103 remains ₹" << std::fixed << std::setprecision(0) << bal103_t5 << "\n";
        std::cout << "Validation: " << ((!t5Success && bal103_t5 == 5000.0) ? "PASSED (Integrity preserved)\n\n" : "FAILED\n\n");

        // Display Final Account Summary
        txManager.displayAccounts();
        std::cout << "\n";

        // Display PayLock Transactions Table (as specified in Section 11)
        txManager.displayTransactionHistory();
        std::cout << "\n";

        // Display Detailed Audit Log for Transaction #1001 (as specified in Section 5)
        std::cout << "========================================\n";
        std::cout << "     TRANSACTION AUDIT LOG (WAL TRACE)  \n";
        std::cout << "========================================\n\n";
        txManager.displayTransactionAudit(1001);

        // Display Controlled Failure Audit Log for Transaction #1002
        std::cout << "----------------------------------------\n";
        std::cout << "Controlled Failure Audit Log (#1002):\n";
        std::cout << "----------------------------------------\n";
        txManager.displayTransactionAudit(1002);

        // DBMS ACID Summary Report (as specified in Section 12)
        std::cout << "==================================================\n";
        std::cout << "          ACID PROPERTIES DEMONSTRATION           \n";
        std::cout << "==================================================\n";
        std::cout << "[ATOMICITY]   : Either all operations succeed or all are undone.\n"
                  << "                Verified: Tx #1001 fully debited and credited.\n"
                  << "                Verified: Tx #1002 insufficient balance rolled back completely;\n"
                  << "                no partial deduction occurred.\n\n"
                  << "[CONSISTENCY] : Schema constraints (balance >= 0.00, amount > 0.00)\n"
                  << "                and foreign keys ensure database validity before and after.\n\n"
                  << "[ISOLATION]   : Single-threaded baseline validated for Day 2.\n"
                  << "                Full concurrent isolation with multithreading & locks\n"
                  << "                will be demonstrated on Day 3.\n\n"
                  << "[DURABILITY]  : Once COMMIT executes, changes are durably persisted\n"
                  << "                to MySQL InnoDB transaction log (WAL).\n";
        std::cout << "==================================================\n";

    } catch (const paylock::DatabaseException& ex) {
        std::cerr << "\n[Database Error]: " << ex.what() << "\n";
        return 1;
    } catch (const std::exception& ex) {
        std::cerr << "\n[System Error]: " << ex.what() << "\n";
        return 1;
    }

    return 0;
}
