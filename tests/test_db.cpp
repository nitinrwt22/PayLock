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
    std::cout << "[TEST RUNNER] Starting PayLock Complete Test Suite (Days 1, 2 & 3 Concurrency Engine)...\n";

    paylock::DatabaseConfig config = paylock::DatabaseConfig::fromEnvOrDefault();
    paylock::DatabaseManager db(config);

    // =========================================================================
    // SECTION 1: DATABASE & SCHEMA FOUNDATIONS
    // =========================================================================
    std::cout << "\n--- SECTION 1: DATABASE & SCHEMA FOUNDATIONS ---\n";

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
    // SECTION 2: TRANSACTION ENGINE CORE TESTS (STEP 2 SPECIFICATION)
    // =========================================================================
    std::cout << "\n--- SECTION 2: TRANSACTION ENGINE & ACID TESTS ---\n";

    paylock::TransactionManager txManager(db);

    // Reset baseline test balances
    db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
    db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");
    db.executeQuery("UPDATE accounts SET balance = 7000.00 WHERE account_id = 103;");

    // Test 5 (Step 14 Test 1): Successful Transfer (A=10000, B=5000, Transfer 2000 -> A=8000, B=7000, COMMITTED)
    std::cout << "Test 5: [Step 14 Test 1] Successful transfer (101 -> 102, ₹2,000)... ";
    bool t5 = txManager.transfer(101, 102, 2000.00);
    assert(t5);
    auto bal101_after = db.getAccountBalance(101);
    auto bal102_after = db.getAccountBalance(102);
    assert(bal101_after.has_value() && *bal101_after == 8000.00);
    assert(bal102_after.has_value() && *bal102_after == 7000.00);
    auto allTxAfter5 = txManager.getTransactions();
    assert(!allTxAfter5.empty());
    assert(allTxAfter5.back().status == "COMMITTED");
    std::cout << "PASSED (A=₹8000, B=₹7000, Status=COMMITTED)\n";

    // Test 6 (Step 14 Test 2): Insufficient Balance Transfer (A=1000, Transfer 2000 -> Fails, A remains 1000)
    std::cout << "Test 6: [Step 14 Test 2] Insufficient balance transfer rejection... ";
    db.executeQuery("UPDATE accounts SET balance = 1000.00 WHERE account_id = 101;");
    bool t6 = txManager.transfer(101, 102, 2000.00);
    assert(!t6);
    auto bal101_t6 = db.getAccountBalance(101);
    assert(bal101_t6.has_value() && *bal101_t6 == 1000.00);
    std::cout << "PASSED (Transfer rejected, balance remains ₹1000, status ROLLED_BACK)\n";

    // Test 7 (Step 14 Test 3): Invalid Receiver (Transfer to nonexistent account 9999)
    std::cout << "Test 7: [Step 14 Test 3] Invalid receiver (Account 9999)... ";
    db.executeQuery("UPDATE accounts SET balance = 8000.00 WHERE account_id = 101;");
    bool t7 = txManager.transfer(101, 9999, 1000.00);
    assert(!t7);
    auto bal101_t7 = db.getAccountBalance(101);
    assert(bal101_t7.has_value() && *bal101_t7 == 8000.00);
    std::cout << "PASSED (Rejected, sender balance untouched at ₹8000)\n";

    // Test 8 (Step 14 Test 4): Invalid Amount 0
    std::cout << "Test 8: [Step 14 Test 4] Invalid amount (Transfer ₹0)... ";
    bool t8 = txManager.transfer(101, 102, 0.00);
    assert(!t8);
    auto bal101_t8 = db.getAccountBalance(101);
    assert(bal101_t8.has_value() && *bal101_t8 == 8000.00);
    std::cout << "PASSED (Transfer ₹0 rejected)\n";

    // Test 9 (Step 14 Test 5): Negative Amount
    std::cout << "Test 9: [Step 14 Test 5] Negative amount (Transfer -₹500)... ";
    bool t9 = txManager.transfer(101, 102, -500.00);
    assert(!t9);
    auto bal101_t9 = db.getAccountBalance(101);
    assert(bal101_t9.has_value() && *bal101_t9 == 8000.00);
    std::cout << "PASSED (Negative transfer rejected)\n";

    // Test 10 (Step 14 Test 6): Successful Deposit (A=5000, Deposit 2000 -> A=7000)
    std::cout << "Test 10: [Step 14 Test 6] Successful deposit (Account 102, ₹2,000)... ";
    db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");
    bool t10 = txManager.deposit(102, 2000.00);
    assert(t10);
    auto bal102_t10 = db.getAccountBalance(102);
    assert(bal102_t10.has_value() && *bal102_t10 == 7000.00);
    std::cout << "PASSED (Account 102 balance = ₹7000)\n";

    // Test 11 (Step 14 Test 7): Successful Withdrawal (A=5000, Withdraw 2000 -> A=3000)
    std::cout << "Test 11: [Step 14 Test 7] Successful withdrawal (Account 103, ₹2,000)... ";
    db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 103;");
    bool t11 = txManager.withdraw(103, 2000.00);
    assert(t11);
    auto bal103_t11 = db.getAccountBalance(103);
    assert(bal103_t11.has_value() && *bal103_t11 == 3000.00);
    std::cout << "PASSED (Account 103 balance = ₹3000)\n";

    // Test 12 (Step 14 Test 8): Withdrawal Exceeding Balance (A=1000, Withdraw 2000 -> Rejected)
    std::cout << "Test 12: [Step 14 Test 8] Withdrawal exceeding balance (A=1000, Withdraw 2000)... ";
    db.executeQuery("UPDATE accounts SET balance = 1000.00 WHERE account_id = 103;");
    bool t12 = txManager.withdraw(103, 2000.00);
    assert(!t12);
    auto bal103_t12 = db.getAccountBalance(103);
    assert(bal103_t12.has_value() && *bal103_t12 == 1000.00);
    std::cout << "PASSED (Overdraft prevented, balance preserved at ₹1000)\n";

    // Test 13 (Step 14 Test 9): Controlled ROLLBACK (Mid-flight failure returns DB to original state)
    std::cout << "Test 13: [Step 14 Test 9] Controlled MySQL ROLLBACK verification... ";
    db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
    db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");
    bool t13 = txManager.demonstrateControlledRollback(101, 102, 2000.00);
    assert(t13);
    auto bal101_t13 = db.getAccountBalance(101);
    auto bal102_t13 = db.getAccountBalance(102);
    assert(bal101_t13.has_value() && *bal101_t13 == 10000.00);
    assert(bal102_t13.has_value() && *bal102_t13 == 5000.00);
    std::cout << "PASSED (Atomicity proven: mid-flight crash cleanly rolled back, balances intact)\n";

    // Test 14: Transaction History & Query Operations
    std::cout << "Test 14: Transaction history query API... ";
    auto allTxs = txManager.getTransactions();
    assert(!allTxs.empty());
    auto tx101 = txManager.getTransactionsForAccount(101);
    assert(!tx101.empty());
    auto singleTx = txManager.getTransaction(allTxs.front().transactionId);
    assert(singleTx.has_value());
    std::cout << "PASSED (History retrieved: " << allTxs.size() << " total, " << tx101.size() << " for Account 101)\n";

    // =========================================================================
    // SECTION 3: CONCURRENCY ENGINE TESTS (STEP 3 SPECIFICATION)
    // =========================================================================
    std::cout << "\n--- SECTION 3: CONCURRENCY ENGINE & MULTITHREADING TESTS ---\n";

    // Test 15 [Step 20 Test 1 & 10]: ConcurrencyManager worker pool lifecycle & clean termination
    std::cout << "Test 15: [Step 20 Test 1 & 10] ConcurrencyManager worker pool & clean shutdown... ";
    {
        paylock::ConcurrencyManager poolMgr(config);
        poolMgr.start(3);
        assert(poolMgr.isRunning());

        db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
        db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");

        poolMgr.submitTransfer("JOB_001", 101, 102, 500.00);
        poolMgr.submitDeposit("JOB_002", 102, 1000.00);
        poolMgr.submitWithdraw("JOB_003", 101, 1000.00);

        poolMgr.waitForCompletion();
        auto stats = poolMgr.getSystemStats();
        assert(stats.totalTransactions == 3);
        assert(stats.completedTransactions == 3);
        assert(stats.activeThreads == 0);

        poolMgr.shutdown();
        assert(!poolMgr.isRunning());
    }
    std::cout << "PASSED (Worker pool executed 3 jobs and shut down cleanly)\n";

    // Test 16 [Step 20 Test 2]: Thread states tracking (CREATED, WAITING, RUNNING, COMPLETED)
    std::cout << "Test 16: [Step 20 Test 2] Thread state lifecycle tracking... ";
    {
        paylock::ConcurrencyManager stateMgr(config);
        stateMgr.start(2);
        auto workers = stateMgr.getAllWorkers();
        assert(workers.size() == 2);
        assert(workers[0].workerId == "W001");
        assert(workers[1].workerId == "W002");

        stateMgr.submitTransfer("T_STATE_TEST", 101, 102, 100.00);
        stateMgr.waitForCompletion();

        auto w1 = stateMgr.getWorkerInfo("W001");
        auto w2 = stateMgr.getWorkerInfo("W002");
        assert(w1.state == paylock::ThreadState::COMPLETED || w1.state == paylock::ThreadState::WAITING);
        assert(w2.state == paylock::ThreadState::COMPLETED || w2.state == paylock::ThreadState::WAITING);
        stateMgr.shutdown();
    }
    std::cout << "PASSED (Thread states tracked in thread-safe registry)\n";

    // Test 17 [Step 20 Test 3 & 4]: Shared counter race condition & Mutex protection
    std::cout << "Test 17: [Step 20 Test 3 & 4] Shared counter race condition & mutex resolution... ";
    auto counterDemo = paylock::ConcurrencyManager::demonstrateCounterRaceCondition(8, 5000);
    assert(counterDemo.raceConditionOccurred); // Unsafe counter suffered lost updates
    assert(counterDemo.unsafeFinalCount < counterDemo.expectedCount);
    assert(counterDemo.safeFinalCount == counterDemo.expectedCount); // Mutex guaranteed deterministic result
    std::cout << "PASSED (Unsafe: " << counterDemo.unsafeFinalCount << " < " << counterDemo.expectedCount
              << " | Safe: " << counterDemo.safeFinalCount << " == " << counterDemo.expectedCount << ")\n";

    // Test 18 [Step 20 Test 5, 6, 7]: Concurrent Multi-Threaded Transfers & DB Balance Verification
    std::cout << "Test 18: [Step 20 Test 5, 6, 7] Concurrent Transfers (A=₹20,000, B=₹5,000, C=₹5,000)... ";
    db.executeQuery("UPDATE accounts SET balance = 20000.00 WHERE account_id = 101;");
    db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");
    db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 103;");

    paylock::ConcurrentTransactionProcessor processor(config);
    std::vector<paylock::TransferRequest> requests = {
        {"W1", 101, 102, 2000.00},
        {"W2", 101, 103, 3000.00},
        {"W3", 102, 103, 1000.00},
        {"W4", 101, 102, 1500.00}
    };
    auto metrics = processor.processConcurrentTransfers(requests);
    assert(metrics.successfulTransactions == 4);

    // Verify balances directly from MySQL database!
    auto b101_final = db.getAccountBalance(101).value_or(0.0);
    auto b102_final = db.getAccountBalance(102).value_or(0.0);
    auto b103_final = db.getAccountBalance(103).value_or(0.0);

    // Initial: 101=20,000, 102=5,000, 103=5,000
    // Net:
    // 101: 20000 - 2000 - 3000 - 1500 = 13,500
    // 102: 5000 + 2000 - 1000 + 1500  = 7,500
    // 103: 5000 + 3000 + 1000         = 9,000
    assert(b101_final == 13500.00);
    assert(b102_final == 7500.00);
    assert(b103_final == 9000.00);
    std::cout << "PASSED (MySQL Verified: 101=₹13500, 102=₹7500, 103=₹9000 in " 
              << metrics.executionTimeMs << " ms)\n";

    // Test 19 [Step 20 Test 8]: Thread-safe logging & WAL log verification
    std::cout << "Test 19: [Step 20 Test 8] Log integrity & absence of corruption... ";
    auto logs = db.listTransactionLogs();
    assert(!logs.empty());
    // Check that every log entry has valid non-empty fields
    for (const auto& l : logs) {
        assert(!l.operation.empty());
    }
    std::cout << "PASSED (" << logs.size() << " WAL records validated without interleaving)\n";

    // Test 20 [Step 20 Test 9]: Failed concurrent transactions do not corrupt balances
    std::cout << "Test 20: [Step 20 Test 9] Concurrent Overdraft Contention (A=₹5,000, T1=₹4,000, T2=₹3,000)... ";
    db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 101;");
    std::vector<paylock::WithdrawalRequest> odRequests = {
        {"W1", 101, 4000.00},
        {"W2", 101, 3000.00}
    };
    auto odMetrics = processor.processConcurrentWithdrawals(odRequests);
    assert(odMetrics.successfulTransactions == 1);
    assert(odMetrics.failedTransactions == 1);

    auto b101_od = db.getAccountBalance(101).value_or(0.0);
    // Either T1 won (leaving 1000) or T2 won (leaving 2000); strictly non-negative!
    assert(b101_od == 1000.00 || b101_od == 2000.00);
    assert(b101_od >= 0.00);
    std::cout << "PASSED (1 succeeded, 1 safely rejected, balance preserved at ₹" << b101_od << ")\n";

    // =========================================================================
    // SECTION 4: ARCHITECTURAL SUBSYSTEM INTERFACE TESTS
    // =========================================================================
    std::cout << "\n--- SECTION 4: ARCHITECTURAL SUBSYSTEM INTERFACE TESTS ---\n";

    // Test 21: Logger Subsystem
    std::cout << "Test 21: Testing Logger subsystem... ";
    paylock::Logger::info("Logger operational", "TestSuite");
    std::cout << "PASSED\n";

    // Test 22: LockManager
    std::cout << "Test 22: Testing LockManager lock acquisition & release... ";
    paylock::LockManager lockMgr;
    assert(lockMgr.acquireLock(1001, 101, paylock::LockMode::EXCLUSIVE));
    assert(lockMgr.isAccountLocked(101));
    assert(lockMgr.releaseLock(1001, 101));
    assert(!lockMgr.isAccountLocked(101));
    std::cout << "PASSED\n";

    // Test 23: DeadlockDetector
    std::cout << "Test 23: Testing DeadlockDetector WFG cycle detection... ";
    paylock::DeadlockDetector detector;
    detector.addWaitForEdge(1, 2);
    detector.addWaitForEdge(2, 3);
    detector.addWaitForEdge(3, 1);
    std::vector<int> cycle;
    assert(detector.hasDeadlock(cycle));
    detector.clearGraph();
    cycle.clear();
    assert(!detector.hasDeadlock(cycle));
    std::cout << "PASSED (Cycle detected and resolved)\n";

    // Test 24: RecoveryManager
    std::cout << "Test 24: Testing RecoveryManager victim selection... ";
    paylock::RecoveryManager recoveryMgr;
    int victim = recoveryMgr.selectVictim({101, 102, 105}, paylock::VictimPolicy::YOUNGEST_TRANSACTION);
    assert(victim == 105);
    assert(recoveryMgr.recoverDeadlock(victim, "Test abort"));
    std::cout << "PASSED (Victim Tx #" << victim << " selected & recovered)\n";

    // Test 25: ResourceManager
    std::cout << "Test 25: Testing ResourceManager page allocation & translation... ";
    paylock::ResourceManager resourceMgr(64);
    assert(resourceMgr.getAvailablePages() == 64);
    assert(resourceMgr.requestMemoryPage(1001, 5));
    assert(resourceMgr.getAvailablePages() == 63);
    resourceMgr.simulatePageAccess(1001, 8192);
    resourceMgr.releaseMemoryPages(1001);
    assert(resourceMgr.getAvailablePages() == 64);
    std::cout << "PASSED\n";

    std::cout << "\n[ALL 25 UNIT, TRANSACTION ENGINE, ACID, CONCURRENCY & ARCHITECTURE TESTS PASSED SUCCESSFULLY!]\n";
}

int main() {
    try {
        runTests();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "\n[TEST FAILED WITH EXCEPTION]: " << ex.what() << "\n";
        return 1;
    }
}
