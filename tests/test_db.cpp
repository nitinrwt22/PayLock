#include "database/DatabaseManager.h"
#include "transaction/TransactionManager.h"
#include "concurrency/ConcurrencyManager.h"
#include "concurrency/ConcurrentTransactionProcessor.h"
#include "locking/LockManager.h"
#include "deadlock/DeadlockDetector.h"
#include "recovery/RecoveryManager.h"
#include "resources/ResourceManager.h"
#include "logging/Logger.h"
#include <iostream>
#include <cassert>
#include <ctime>

void runTests() {
    std::cout << "[TEST RUNNER] Starting Day 1, Day 2 & Day 3 PayLock Automated Tests...\n";

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

    // =========================================================================
    // DAY 3 CONCURRENCY & MULTITHREADING TESTS
    // =========================================================================

    std::cout << "\n--- DAY 3 CONCURRENCY & SYNCHRONIZATION TESTS ---\n";
    paylock::ConcurrentTransactionProcessor processor(config);

    // Test 12: Concurrent Multi-Threaded Transfers
    std::cout << "Test 12: Concurrent Transfers (4 threads, real DB connections)... ";
    db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
    db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");
    db.executeQuery("UPDATE accounts SET balance = 7000.00 WHERE account_id = 103;");

    std::vector<paylock::TransferRequest> requests = {
        {"T1", 101, 102, 2000.00},
        {"T2", 101, 103, 3000.00},
        {"T3", 102, 103, 1000.00},
        {"T4", 103, 101,  500.00}
    };
    auto metrics = processor.processConcurrentTransfers(requests);
    assert(metrics.successfulTransactions == 4);

    auto b101_final = db.getAccountBalance(101).value_or(0.0);
    auto b102_final = db.getAccountBalance(102).value_or(0.0);
    auto b103_final = db.getAccountBalance(103).value_or(0.0);

    assert(b101_final == 5500.00);
    assert(b102_final == 6000.00);
    assert(b103_final == 10500.00);
    std::cout << "PASSED (All 4 threads completed: 101=₹5500, 102=₹6000, 103=₹10500 in " 
              << metrics.executionTimeMs << " ms)\n";

    // Test 13: Same-Account Concurrent Contention (3 concurrent withdrawals on Account 101)
    std::cout << "Test 13: Same-Account Concurrent Contention (3 withdrawals on Account 101)... ";
    db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
    std::vector<paylock::WithdrawalRequest> wRequests = {
        {"T1", 101, 2000.00},
        {"T2", 101, 3000.00},
        {"T3", 101, 4000.00}
    };
    auto wMetrics = processor.processConcurrentWithdrawals(wRequests);
    assert(wMetrics.successfulTransactions == 3);
    auto b101_afterW = db.getAccountBalance(101).value_or(0.0);
    assert(b101_afterW == 1000.00);
    std::cout << "PASSED (Account 101 correctly debited to ₹1000)\n";

    // Test 14: Concurrent Overdraft Contention (Total ₹13,000 requested on ₹10,000 balance)
    std::cout << "Test 14: Concurrent Overdraft Contention (Exactly 1 succeeds, 1 rejected)... ";
    db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
    std::vector<paylock::WithdrawalRequest> odRequests = {
        {"T1", 101, 7000.00},
        {"T2", 101, 6000.00}
    };
    auto odMetrics = processor.processConcurrentWithdrawals(odRequests);
    assert(odMetrics.successfulTransactions == 1);
    assert(odMetrics.failedTransactions == 1);
    auto b101_od = db.getAccountBalance(101).value_or(0.0);
    assert(b101_od == 3000.00 || b101_od == 4000.00);
    std::cout << "PASSED (Overdraft prevented: 1 successful, 1 failed, balance remains ₹" 
              << b101_od << ")\n";

    // =========================================================================
    // MODULAR SUBSYSTEM INTERFACE TESTS (FOUNDATION FOR DAY 4+)
    // =========================================================================

    std::cout << "\n--- ARCHITECTURAL SUBSYSTEM INTERFACE TESTS ---\n";

    // Test 15: Logger Subsystem
    std::cout << "Test 15: Testing Logger subsystem... ";
    paylock::Logger::setLogLevel(paylock::LogLevel::DEBUG);
    assert(paylock::Logger::getLogLevel() == paylock::LogLevel::DEBUG);
    paylock::Logger::info("Logger operational", "TestSuite");
    paylock::Logger::setLogLevel(paylock::LogLevel::INFO);
    std::cout << "PASSED\n";

    // Test 16: LockManager Interface
    std::cout << "Test 16: Testing LockManager lock acquisition & release... ";
    paylock::LockManager lockMgr;
    assert(!lockMgr.isAccountLocked(101));
    bool lockAcquired = lockMgr.acquireLock(1001, 101, paylock::LockMode::EXCLUSIVE);
    assert(lockAcquired);
    assert(lockMgr.isAccountLocked(101));
    bool lockReleased = lockMgr.releaseLock(1001, 101);
    assert(lockReleased);
    assert(!lockMgr.isAccountLocked(101));
    std::cout << "PASSED\n";

    // Test 17: DeadlockDetector Cycle Detection
    std::cout << "Test 17: Testing DeadlockDetector WFG cycle detection... ";
    paylock::DeadlockDetector detector;
    // Build cycle: T1 -> T2 -> T3 -> T1
    detector.addWaitForEdge(1, 2);
    detector.addWaitForEdge(2, 3);
    detector.addWaitForEdge(3, 1);
    std::vector<int> cycle;
    bool hasDeadlock = detector.hasDeadlock(cycle);
    assert(hasDeadlock);
    assert(!cycle.empty());
    // Break cycle by removing T1
    detector.removeTransaction(1);
    std::vector<int> cycleAfter;
    assert(!detector.hasDeadlock(cycleAfter));
    std::cout << "PASSED (Cycle detected and resolved)\n";

    // Test 18: RecoveryManager Victim Selection
    std::cout << "Test 18: Testing RecoveryManager victim selection... ";
    paylock::RecoveryManager recoveryMgr;
    std::vector<int> deadlockedTxs = {101, 105, 103};
    int victim = recoveryMgr.selectVictim(deadlockedTxs, paylock::VictimPolicy::YOUNGEST_TRANSACTION);
    assert(victim == 105);
    bool recovered = recoveryMgr.recoverDeadlock(victim, "Test abort");
    assert(recovered);
    assert(recoveryMgr.getRecoveryCount() == 1);
    std::cout << "PASSED (Victim Tx #" << victim << " selected & recovered)\n";

    // Test 19: ResourceManager Memory Page Management
    std::cout << "Test 19: Testing ResourceManager page allocation & translation... ";
    paylock::ResourceManager resMgr(16);
    assert(resMgr.getAvailablePages() == 16);
    bool pageAllocated = resMgr.requestMemoryPage(1001, 5);
    assert(pageAllocated);
    assert(resMgr.getAvailablePages() == 15);
    resMgr.simulatePageAccess(1001, 8192); // Page 2, Offset 0
    resMgr.releaseMemoryPages(1001);
    assert(resMgr.getAvailablePages() == 16);
    std::cout << "PASSED\n";

    std::cout << "\n[ALL 19 UNIT, INTEGRATION, CONCURRENCY & ARCHITECTURE TESTS PASSED SUCCESSFULLY!]\n";
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
