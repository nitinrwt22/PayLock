#include "../src/database/DatabaseManager.h"
#include "../src/transaction/TransactionManager.h"
#include <iostream>
#include <cassert>
#include <ctime>

void runTests() {
    std::cout << "[TEST RUNNER] Starting Day 1 & Day 2 PayLock Automated Tests...\n";

    paylock::DatabaseConfig config = paylock::DatabaseConfig::fromEnvOrDefault();
    paylock::DatabaseManager db(config);

    // =========================================================================
    // DAY 1 FOUNDATIONAL TESTS
    // =========================================================================

    // Test 1: Connection
    std::cout << "Test 1: Establishing connection... ";
    db.connect();
    assert(db.isConnected());
    std::cout << "PASSED\n";

    // Test 2: Fetch Seed Accounts
    std::cout << "Test 2: Verifying seed accounts count... ";
    auto accounts = db.listAccounts();
    assert(accounts.size() >= 3);
    std::cout << "PASSED (" << accounts.size() << " accounts found)\n";

    // Test 3: Check Account 101 Balance
    std::cout << "Test 3: Checking Account 101 balance... ";
    auto b101 = db.getAccountBalance(101);
    assert(b101.has_value());
    std::cout << "PASSED (₹" << *b101 << ")\n";

    // Test 4: Verify DBMS Integrity Constraint (Negative Balance Rejected)
    std::cout << "Test 4: Verifying DBMS Constraint (Negative balance should fail)... ";
    int tempUserId = db.createUser("ConstraintTestUser", "chk_test_" + std::to_string(std::time(nullptr)) + "@paylock.local");
    assert(tempUserId > 0);
    bool caughtConstraintError = false;
    try {
        db.createAccount(998, tempUserId, -500.00);
    } catch (const paylock::DatabaseException&) {
        caughtConstraintError = true;
    }
    assert(caughtConstraintError);
    db.executeQuery("DELETE FROM users WHERE user_id = " + std::to_string(tempUserId) + ";");
    std::cout << "PASSED (chk_account_balance enforced)\n";

    // =========================================================================
    // DAY 2 TRANSACTION & ACID TESTS
    // =========================================================================

    paylock::TransactionManager txManager(db);

    // Reset baseline test balances
    db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
    db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");
    db.executeQuery("UPDATE accounts SET balance = 7000.00 WHERE account_id = 103;");

    // Test 5: Successful Atomic Transfer (101 -> 102, ₹2,000)
    std::cout << "Test 5: Transfer ₹2,000 from 101 -> 102 (Happy Path)... ";
    bool t5 = txManager.transfer(101, 102, 2000.00);
    assert(t5);
    auto bal101_after = db.getAccountBalance(101);
    auto bal102_after = db.getAccountBalance(102);
    assert(bal101_after.has_value() && *bal101_after == 8000.00);
    assert(bal102_after.has_value() && *bal102_after == 7000.00);
    std::cout << "PASSED (101=₹8000, 102=₹7000, COMMITTED)\n";

    // Test 6: Controlled Failure - Insufficient Balance (101 -> 103, ₹15,000)
    std::cout << "Test 6: Transfer ₹15,000 from 101 -> 103 (Insufficient Balance)... ";
    bool t6 = txManager.transfer(101, 103, 15000.00);
    assert(!t6);
    auto bal101_t6 = db.getAccountBalance(101);
    auto bal103_t6 = db.getAccountBalance(103);
    assert(bal101_t6.has_value() && *bal101_t6 == 8000.00);
    assert(bal103_t6.has_value() && *bal103_t6 == 7000.00);
    std::cout << "PASSED (Rollback verified, balances intact)\n";

    // Test 7: Controlled Failure - Invalid Receiver Account
    std::cout << "Test 7: Transfer to non-existent account (Account 9999)... ";
    bool t7 = txManager.transfer(101, 9999, 1000.00);
    assert(!t7);
    auto bal101_t7 = db.getAccountBalance(101);
    assert(bal101_t7.has_value() && *bal101_t7 == 8000.00);
    std::cout << "PASSED (Rejected without debiting sender)\n";

    // Test 8: Deposit Operation
    std::cout << "Test 8: Deposit ₹1,000 into Account 102... ";
    bool t8 = txManager.deposit(102, 1000.00);
    assert(t8);
    auto bal102_t8 = db.getAccountBalance(102);
    assert(bal102_t8.has_value() && *bal102_t8 == 8000.00);
    std::cout << "PASSED (102=₹8000)\n";

    // Test 9: Withdrawal Operation
    std::cout << "Test 9: Withdraw ₹2,000 from Account 103... ";
    bool t9 = txManager.withdraw(103, 2000.00);
    assert(t9);
    auto bal103_t9 = db.getAccountBalance(103);
    assert(bal103_t9.has_value() && *bal103_t9 == 5000.00);
    std::cout << "PASSED (103=₹5000)\n";

    // Test 10: Withdrawal Exceeding Balance
    std::cout << "Test 10: Withdraw ₹10,000 from Account 103 (Overdraft attempt)... ";
    bool t10 = txManager.withdraw(103, 10000.00);
    assert(!t10);
    auto bal103_t10 = db.getAccountBalance(103);
    assert(bal103_t10.has_value() && *bal103_t10 == 5000.00);
    std::cout << "PASSED (Overdraft prevented, balance unchanged)\n";

    // Test 11: Transaction Logs Audit Trail Presence
    std::cout << "Test 11: Verifying WAL audit log entries exist... ";
    auto allLogs = db.listTransactionLogs();
    assert(!allLogs.empty());
    std::cout << "PASSED (" << allLogs.size() << " log records verified)\n";

    std::cout << "\n[ALL 11 UNIT & INTEGRATION TESTS PASSED SUCCESSFULLY!]\n";
}

int main() {
    try {
        runTests();
    } catch (const std::exception& ex) {
        std::cerr << "[TEST FAILED]: " << ex.what() << "\n";
        return 1;
    }
    return 0;
}
