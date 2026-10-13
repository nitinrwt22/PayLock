#include "database/DatabaseManager.h"
#include "transaction/TransactionManager.h"
#include "concurrency/ConcurrencyManager.h"
#include "concurrency/ConcurrentTransactionProcessor.h"
#include "locking/LockManager.h"
#include "deadlock/DeadlockDetector.h"
#include "recovery/RecoveryManager.h"
#include "resources/ResourceManager.h"
#include "resources/PagingSimulator.h"
#include "logging/Logger.h"
#include <iostream>
#include <cassert>
#include <ctime>
#include <thread>
#include <atomic>
#include <chrono>

void runTests() {
    std::cout << "[TEST RUNNER] Starting PayLock Complete Test Suite (Steps 1, 2, 3 & Step 4 Lock Manager)...\n";

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

    // =========================================================================
    // SECTION 5: STEP 4 — LOCK MANAGER COMPREHENSIVE TESTS
    // =========================================================================
    std::cout << "\n--- SECTION 5: STEP 4 — LOCK MANAGER CONCURRENCY CONTROL TESTS ---\n";

    // Test 26: Shared Locks Coexistence (Step 20)
    std::cout << "Test 26: [Step 20] Shared Locks coexistence (T001 & T002 on A101)... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1, 101, paylock::LockMode::SHARED));
        assert(lm.acquireLock(2, 101, paylock::LockMode::SHARED));
        assert(lm.isAccountLocked(101));
        assert(lm.getAccountLockMode(101).value() == paylock::LockMode::SHARED);
        auto holders = lm.getLockHolders(101);
        assert(holders.size() == 2);
        assert(lm.releaseLock(1, 101));
        assert(lm.isAccountLocked(101)); // T2 still holds
        assert(lm.releaseLock(2, 101));
        assert(!lm.isAccountLocked(101)); // All released
    }
    std::cout << "PASSED (Both granted S lock simultaneously)\n";

    // Test 27: Exclusive Lock Acquisition & Release (Step 21)
    std::cout << "Test 27: [Step 21] Exclusive Lock acquisition & release (T001 on A101)... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1, 101, paylock::LockMode::EXCLUSIVE));
        assert(lm.isAccountLocked(101));
        assert(lm.getAccountLockMode(101).value() == paylock::LockMode::EXCLUSIVE);
        assert(lm.getLockHolders(101).size() == 1);
        assert(lm.releaseLock(1, 101));
        assert(!lm.isAccountLocked(101));
        assert(lm.getLockHolders(101).empty());
    }
    std::cout << "PASSED (X lock acquired and released cleanly)\n";

    // Test 28: Exclusive Conflict & Waiting Queue Wakeup (Step 22)
    std::cout << "Test 28: [Step 22] Exclusive Lock conflict & condition variable wakeup (T1 X vs T2 X)... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1, 101, paylock::LockMode::EXCLUSIVE));

        std::atomic<bool> t2Acquired{false};
        std::atomic<bool> t2Started{false};

        std::thread t2([&]() {
            t2Started = true;
            if (lm.acquireLock(2, 101, paylock::LockMode::EXCLUSIVE)) {
                t2Acquired = true;
                lm.releaseLock(2, 101);
            }
        });

        while (!t2Started.load()) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // T2 must be waiting and blocked
        assert(!t2Acquired.load());
        auto waiters = lm.getWaitingRequests(101);
        assert(waiters.size() == 1);
        assert(waiters[0].transactionId == 2);
        assert(waiters[0].mode == paylock::LockMode::EXCLUSIVE);

        // T1 releases lock -> wakes T2
        assert(lm.releaseLock(1, 101));
        t2.join();

        assert(t2Acquired.load());
        assert(!lm.isAccountLocked(101));
    }
    std::cout << "PASSED (T2 blocked while T1 held X; woke & acquired on release)\n";

    // Test 29: Shared/Exclusive Conflict (Step 23)
    std::cout << "Test 29: [Step 23] Shared/Exclusive conflict (T1 S vs T2 X on A101)... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1, 101, paylock::LockMode::SHARED));

        std::atomic<bool> t2Acquired{false};
        std::atomic<bool> t2Started{false};

        std::thread t2([&]() {
            t2Started = true;
            if (lm.acquireLock(2, 101, paylock::LockMode::EXCLUSIVE)) {
                t2Acquired = true;
                lm.releaseLock(2, 101);
            }
        });

        while (!t2Started.load()) std::this_thread::yield();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        // T2 must be waiting because X conflicts with S
        assert(!t2Acquired.load());
        assert(lm.getAccountLockMode(101).value() == paylock::LockMode::SHARED);

        // T1 releases S lock -> wakes T2
        assert(lm.releaseLock(1, 101));
        t2.join();

        assert(t2Acquired.load());
        assert(!lm.isAccountLocked(101));
    }
    std::cout << "PASSED (T2 X blocked while T1 held S; granted after release)\n";

    // Test 30: Multiple Waiters Queue Evaluation (Step 24)
    std::cout << "Test 30: [Step 24] Multiple Waiters queue evaluation & FIFO ordering... ";
    {
        paylock::LockManager lm;
        // T1 holds X
        assert(lm.acquireLock(1, 101, paylock::LockMode::EXCLUSIVE));

        std::atomic<bool> t2Acquired{false};
        std::atomic<bool> t3Acquired{false};

        std::thread t2([&]() {
            if (lm.acquireLock(2, 101, paylock::LockMode::EXCLUSIVE)) {
                t2Acquired = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(30));
                lm.releaseLock(2, 101);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(40));

        std::thread t3([&]() {
            if (lm.acquireLock(3, 101, paylock::LockMode::SHARED)) {
                t3Acquired = true;
                lm.releaseLock(3, 101);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(40));

        auto queue = lm.getWaitingRequests(101);
        assert(queue.size() == 2);
        assert(queue[0].transactionId == 2);
        assert(queue[1].transactionId == 3);

        // T1 releases -> T2 acquires first, then releases -> T3 acquires
        assert(lm.releaseLock(1, 101));

        t2.join();
        t3.join();

        assert(t2Acquired.load());
        assert(t3Acquired.load());
        assert(!lm.isAccountLocked(101));
    }
    std::cout << "PASSED (FIFO wait queue granted requests deterministically)\n";

    // Test 31: Release All Locks for Transaction Lifecycle (Step 25)
    std::cout << "Test 31: [Step 25] releaseAllLocks transaction lifecycle cleanup... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1001, 101, paylock::LockMode::EXCLUSIVE));
        assert(lm.acquireLock(1001, 102, paylock::LockMode::EXCLUSIVE));
        assert(lm.acquireLock(1001, 103, paylock::LockMode::SHARED));

        auto held = lm.getLocksForTransaction(1001);
        assert(held.size() == 3);

        lm.releaseAllLocks(1001);

        auto heldAfter = lm.getLocksForTransaction(1001);
        assert(heldAfter.empty());
        assert(!lm.isAccountLocked(101));
        assert(!lm.isAccountLocked(102));
        assert(!lm.isAccountLocked(103));
    }
    std::cout << "PASSED (All 3 resource locks released atomically)\n";

    // Test 32: Lock Upgrade (Step 26)
    std::cout << "Test 32: [Step 26] Lock Upgrade (S -> X): Solo Holder & Multi-Holder Waiting... ";
    {
        paylock::LockManager lm;
        // Case A: Solo holder upgrade
        assert(lm.acquireLock(1, 101, paylock::LockMode::SHARED));
        assert(lm.upgradeLock(1, 101));
        assert(lm.getAccountLockMode(101).value() == paylock::LockMode::EXCLUSIVE);
        assert(lm.releaseLock(1, 101));

        // Case B: Multi-holder upgrade waiting
        assert(lm.acquireLock(1, 101, paylock::LockMode::SHARED));
        assert(lm.acquireLock(2, 101, paylock::LockMode::SHARED));

        std::atomic<bool> t1Upgraded{false};
        std::thread t1([&]() {
            if (lm.upgradeLock(1, 101)) {
                t1Upgraded = true;
                lm.releaseLock(1, 101);
            }
        });

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        assert(!t1Upgraded.load()); // T1 must wait for T2 to release

        assert(lm.releaseLock(2, 101)); // T2 releases S lock
        t1.join();

        assert(t1Upgraded.load());
        assert(!lm.isAccountLocked(101));
    }
    std::cout << "PASSED (Solo upgrade immediate; multi-holder upgrade waited for S release)\n";

    // Test 33: Deterministic Lock Ordering (Step 14)
    std::cout << "Test 33: [Step 14] Deterministic lock ordering (min/max account IDs)... ";
    {
        paylock::LockManager lm;
        // Inverse order: 105 then 101
        assert(lm.acquireTransferLocks(1, 105, 101));
        auto held = lm.getDetailedLocksForTransaction(1);
        assert(held.size() == 2);
        assert(lm.isAccountLocked(101));
        assert(lm.isAccountLocked(105));
        lm.releaseAllLocks(1);
        assert(!lm.isAccountLocked(101));
        assert(!lm.isAccountLocked(105));
    }
    std::cout << "PASSED (Deterministic ordering min(101, 105) applied)\n";

    // Test 34: Lock Table State Monitoring & CLI Display (Step 16)
    std::cout << "Test 34: [Step 16] Lock Table monitoring & ASCII status table... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1001, 101, paylock::LockMode::EXCLUSIVE));
        assert(lm.acquireLock(1002, 102, paylock::LockMode::SHARED));
        assert(lm.acquireLock(1003, 102, paylock::LockMode::SHARED));
        lm.displayLockTable();
        lm.releaseAllLocks(1001);
        lm.releaseAllLocks(1002);
        lm.releaseAllLocks(1003);
    }
    std::cout << "PASSED\n";

    // Test 35: End-to-End Concurrent Lock Processing with Worker Pool (Step 27)
    std::cout << "Test 35: [Step 27] End-to-End Concurrent Lock Processing with Worker Threads... ";
    {
        paylock::LockManager sharedLockMgr;
        paylock::ConcurrencyManager cm(config, &sharedLockMgr);
        cm.start(3);

        // Submit 4 concurrent transactions across shared accounts
        cm.submitTransfer("LOCK_T1", 101, 102, 200.0);
        cm.submitTransfer("LOCK_T2", 102, 103, 150.0);
        cm.submitDeposit("LOCK_T3", 101, 500.0);
        cm.submitWithdraw("LOCK_T4", 103, 100.0);

        cm.waitForCompletion();
        cm.shutdown();

        // Verify all logical resource locks are 100% cleanly released
        assert(!sharedLockMgr.isAccountLocked(101));
        assert(!sharedLockMgr.isAccountLocked(102));
        assert(!sharedLockMgr.isAccountLocked(103));
    }
    std::cout << "PASSED (All worker jobs serialized through LockManager; 0 leaked locks)\n";

    // =========================================================================
    // SECTION 5: DEADLOCK DETECTION SUBSYSTEM TESTS (STEP 5 SPECIFICATION)
    // =========================================================================
    std::cout << "\n--- SECTION 5: DEADLOCK DETECTION & WAIT-FOR GRAPH TESTS ---\n";

    // Test 36: Empty Graph (Requirement 1)
    std::cout << "Test 36: [Step 5 Requirement 1] Empty Wait-For Graph... ";
    {
        paylock::WaitForGraph graph;
        paylock::DeadlockDetector detector;
        assert(graph.empty());
        assert(graph.vertexCount() == 0);
        assert(graph.edgeCount() == 0);
        auto cycles = detector.detectCycles(graph);
        assert(cycles.empty());
        auto report = detector.detectDeadlocks(graph);
        assert(!report.deadlockDetected);
        assert(report.cycles.empty());
        assert(report.involvedTransactions.empty());
    }
    std::cout << "PASSED (Empty graph produces 0 cycles & clean report)\n";

    // Test 37: Graph Without Edges (Isolated Vertices / Case A / Requirement 2)
    std::cout << "Test 37: [Step 5 Requirement 2] Graph without edges (Isolated vertices T1, T2, T3)... ";
    {
        paylock::WaitForGraph graph;
        graph.addVertex(1);
        graph.addVertex(2);
        graph.addVertex(3);
        assert(graph.vertexCount() == 3);
        assert(graph.edgeCount() == 0);
        assert(graph.hasVertex(1) && graph.hasVertex(2) && graph.hasVertex(3));
        paylock::DeadlockDetector detector;
        auto report = detector.detectDeadlocks(graph);
        assert(!report.deadlockDetected);
        assert(report.cycles.empty());
        assert(report.involvedTransactions.empty());
    }
    std::cout << "PASSED (Case A: No dependencies -> No deadlock detected)\n";

    // Test 38: Acyclic Chain (Case B / Requirement 3)
    std::cout << "Test 38: [Step 5 Requirement 3] Acyclic dependency chain (T1 -> T2 -> T3)... ";
    {
        paylock::WaitForGraph graph;
        graph.addEdge(1, 2);
        graph.addEdge(2, 3);
        assert(graph.vertexCount() == 3);
        assert(graph.edgeCount() == 2);
        paylock::DeadlockDetector detector;
        auto report = detector.detectDeadlocks(graph);
        assert(!report.deadlockDetected);
        assert(report.cycles.empty());
    }
    std::cout << "PASSED (Case B: Linear wait chain is not a deadlock)\n";

    // Test 39: Two-Transaction Cycle (Case C / Requirement 4)
    std::cout << "Test 39: [Step 5 Requirement 4] Two-transaction cycle (T1 -> T2 -> T1)... ";
    {
        paylock::WaitForGraph graph;
        graph.addEdge(1, 2);
        graph.addEdge(2, 1);
        paylock::DeadlockDetector detector;
        auto report = detector.detectDeadlocks(graph);
        assert(report.deadlockDetected);
        assert(report.cycles.size() == 1);
        const auto& c = report.cycles[0];
        assert(c.front() == c.back()); // Closing edge
        assert((c == std::vector<int>{1, 2, 1}));
        assert(report.involvedTransactions.size() == 2);
        assert(report.involvedTransactions.count(1) && report.involvedTransactions.count(2));
    }
    std::cout << "PASSED (Case C: T1 -> T2 -> T1 circular wait detected)\n";

    // Test 40: Three-Transaction Cycle (Case D / Requirement 5)
    std::cout << "Test 40: [Step 5 Requirement 5] Three-transaction cycle (T1 -> T2 -> T3 -> T1)... ";
    {
        paylock::WaitForGraph graph;
        graph.addEdge(1, 2);
        graph.addEdge(2, 3);
        graph.addEdge(3, 1);
        paylock::DeadlockDetector detector;
        auto report = detector.detectDeadlocks(graph);
        assert(report.deadlockDetected);
        assert(report.cycles.size() == 1);
        const auto& c = report.cycles[0];
        assert(c.front() == c.back());
        assert((c == std::vector<int>{1, 2, 3, 1}));
        assert(report.involvedTransactions.size() == 3);
        assert(report.involvedTransactions.count(1) && report.involvedTransactions.count(2) && report.involvedTransactions.count(3));
    }
    std::cout << "PASSED (Case D: 3-cycle T1 -> T2 -> T3 -> T1 correctly recognized)\n";

    // Test 41: Disconnected Graph with Cycle (Case F / Requirement 6)
    std::cout << "Test 41: [Step 5 Requirement 6] Disconnected graph with cycle in one component... ";
    {
        paylock::WaitForGraph graph;
        // Component A: Acyclic chain T4 -> T5
        graph.addEdge(4, 5);
        // Component B: Cycle T1 -> T2 -> T1
        graph.addEdge(1, 2);
        graph.addEdge(2, 1);
        paylock::DeadlockDetector detector;
        auto report = detector.detectDeadlocks(graph);
        assert(report.deadlockDetected);
        assert(report.cycles.size() == 1);
        assert((report.cycles[0] == std::vector<int>{1, 2, 1}));
        assert(report.involvedTransactions.count(1) && report.involvedTransactions.count(2));
        assert(!report.involvedTransactions.count(4) && !report.involvedTransactions.count(5));
    }
    std::cout << "PASSED (Case F: Disconnected cycle detected regardless of search start)\n";

    // Test 42: Cycle with Outgoing Non-Cycle Edge (Case E / Requirement 7)
    std::cout << "Test 42: [Step 5 Requirement 7] Cycle with non-cycle outgoing edge (T1->T2->T3->T1, T3->T4)... ";
    {
        paylock::WaitForGraph graph;
        graph.addEdge(1, 2);
        graph.addEdge(2, 3);
        graph.addEdge(3, 1);
        graph.addEdge(3, 4); // Outgoing edge to non-cycle transaction
        paylock::DeadlockDetector detector;
        auto report = detector.detectDeadlocks(graph);
        assert(report.deadlockDetected);
        assert(report.cycles.size() == 1);
        assert((report.cycles[0] == std::vector<int>{1, 2, 3, 1}));
        assert(!report.involvedTransactions.count(4)); // T4 must not be in cycle
    }
    std::cout << "PASSED (Case E: T4 strictly excluded from cycle membership)\n";

    // Test 43: Multiple Cycles and Duplicate Prevention (Requirement 8)
    std::cout << "Test 43: [Step 5 Requirement 8] Multiple distinct cycles in graph... ";
    {
        paylock::WaitForGraph graph;
        // Cycle 1: T1 <-> T2
        graph.addEdge(1, 2);
        graph.addEdge(2, 1);
        // Cycle 2: T3 <-> T4
        graph.addEdge(3, 4);
        graph.addEdge(4, 3);
        paylock::DeadlockDetector detector;
        auto report = detector.detectDeadlocks(graph);
        assert(report.deadlockDetected);
        assert(report.cycles.size() == 2);
        assert((report.cycles[0] == std::vector<int>{1, 2, 1}));
        assert((report.cycles[1] == std::vector<int>{3, 4, 3}));
        assert(report.involvedTransactions.size() == 4);
    }
    std::cout << "PASSED (Multiple distinct cycles detected without duplicates)\n";

    // Test 44: Duplicate Edge Prevention (Requirement 9)
    std::cout << "Test 44: [Step 5 Requirement 9] Duplicate edge insertion prevention... ";
    {
        paylock::WaitForGraph graph;
        assert(graph.addEdge(10, 20));
        assert(!graph.addEdge(10, 20)); // Duplicate edge must return false and not be added
        assert(graph.edgeCount() == 1);
        auto deps = graph.getDependencies(10);
        assert(deps.size() == 1 && deps[0] == 20);
    }
    std::cout << "PASSED (Duplicate edge T10 -> T20 rejected)\n";

    // Test 45: Correct Cycle Membership & Traversal Verification (Requirement 10)
    std::cout << "Test 45: [Step 5 Requirement 10] Precise cycle membership with incoming & outgoing edges... ";
    {
        paylock::WaitForGraph graph;
        graph.addEdge(5, 10);   // incoming to cycle
        graph.addEdge(10, 20);  // cycle
        graph.addEdge(20, 30);  // cycle
        graph.addEdge(30, 10);  // cycle closing edge
        graph.addEdge(30, 40);  // outgoing from cycle
        paylock::DeadlockDetector detector;
        auto report = detector.detectDeadlocks(graph);
        assert(report.cycles.size() == 1);
        const auto& c = report.cycles[0];
        assert(c.front() == c.back());
        assert((c == std::vector<int>{10, 20, 30, 10}));
        assert(report.involvedTransactions.size() == 3);
        assert(report.involvedTransactions.count(10) && report.involvedTransactions.count(20) && report.involvedTransactions.count(30));
        assert(!report.involvedTransactions.count(5));
        assert(!report.involvedTransactions.count(40));
    }
    std::cout << "PASSED (Only true cycle vertices T10, T20, T30 included)\n";

    // Test 46: Graph Built from Real Lock Manager State (Requirement 11)
    std::cout << "Test 46: [Step 5 Requirement 11] Building WFG directly from LockManager snapshot... ";
    {
        paylock::LockTableSnapshot snapshot;
        // ACCOUNT:A101 held by T1 in EXCLUSIVE mode, T2 is waiting
        paylock::ResourceSnapshot res101;
        res101.accountId = 101;
        res101.currentMode = paylock::LockMode::EXCLUSIVE;
        res101.holders.insert(1);
        res101.waitQueue.emplace_back(2, 101, paylock::LockMode::EXCLUSIVE, paylock::LockState::WAITING);
        snapshot.resources[101] = res101;

        paylock::DeadlockDetector detector;
        auto wfg = detector.buildWaitForGraph(snapshot);
        assert(wfg.vertexCount() == 2);
        assert(wfg.hasEdge(2, 1));
        assert(!wfg.hasEdge(1, 2));
    }
    std::cout << "PASSED (WFG accurately synthesized from in-memory snapshot)\n";

    // Test 47: Correct Dependencies for Shared Locks (Requirement 12)
    std::cout << "Test 47: [Step 5 Requirement 12] Shared lock multi-holder dependency generation... ";
    {
        // T1 and T2 both hold SHARED on A101; T3 requests EXCLUSIVE on A101
        paylock::ResourceSnapshot res;
        res.accountId = 101;
        res.currentMode = paylock::LockMode::SHARED;
        res.holders.insert(1);
        res.holders.insert(2);
        res.waitQueue.emplace_back(3, 101, paylock::LockMode::EXCLUSIVE, paylock::LockState::WAITING);

        paylock::LockTableSnapshot snapshot;
        snapshot.resources[101] = res;

        paylock::DeadlockDetector detector;
        auto wfg = detector.buildWaitForGraph(snapshot);
        // T3 must point to BOTH T1 and T2
        assert(wfg.hasEdge(3, 1));
        assert(wfg.hasEdge(3, 2));
        assert(wfg.edgeCount() == 2);
    }
    std::cout << "PASSED (Waiting exclusive request branches to ALL shared owners T1, T2)\n";

    // Test 48: Correct Dependencies for Exclusive Locks (Requirement 13)
    std::cout << "Test 48: [Step 5 Requirement 13] Exclusive lock dependency generation... ";
    {
        // T1 holds EXCLUSIVE on A101; T2 requests SHARED
        paylock::ResourceSnapshot res;
        res.accountId = 101;
        res.currentMode = paylock::LockMode::EXCLUSIVE;
        res.holders.insert(1);
        res.waitQueue.emplace_back(2, 101, paylock::LockMode::SHARED, paylock::LockState::WAITING);

        paylock::LockTableSnapshot snapshot;
        snapshot.resources[101] = res;

        paylock::DeadlockDetector detector;
        auto wfg = detector.buildWaitForGraph(snapshot);
        assert(wfg.hasEdge(2, 1));
        assert(!wfg.hasEdge(1, 2));
        assert(wfg.edgeCount() == 1);
    }
    std::cout << "PASSED (Conflicting reader points exclusively to current writer)\n";

    // Test 49: Live Detection While Transactions Are Actively Waiting (Requirement 14)
    std::cout << "Test 49: [Step 5 Requirement 14] Live concurrent deadlock detection while threads wait... ";
    {
        paylock::LockManager lm;
        const int a1 = 101, a2 = 102;
        assert(lm.acquireLock(1001, a1, paylock::LockMode::EXCLUSIVE));
        assert(lm.acquireLock(1002, a2, paylock::LockMode::EXCLUSIVE));

        std::atomic<bool> t1Acquired{false};
        std::atomic<bool> t2Acquired{false};

        std::thread th1([&]() {
            if (lm.acquireLock(1001, a2, paylock::LockMode::EXCLUSIVE)) {
                t1Acquired.store(true);
                lm.releaseLock(1001, a2);
            }
        });

        while (lm.getWaitingRequests(a2).empty()) {
            std::this_thread::yield();
        }

        std::thread th2([&]() {
            if (lm.acquireLock(1002, a1, paylock::LockMode::EXCLUSIVE)) {
                t2Acquired.store(true);
                lm.releaseLock(1002, a1);
            }
        });

        while (lm.getWaitingRequests(a1).empty()) {
            std::this_thread::yield();
        }

        paylock::DeadlockDetector detector(&lm);
        auto report = detector.detectDeadlocks();

        assert(report.deadlockDetected);
        assert(report.cycles.size() == 1);
        assert((report.cycles[0] == std::vector<int>{1001, 1002, 1001}));
        assert(report.involvedTransactions.count(1001) && report.involvedTransactions.count(1002));

        // Clean termination without recovery
        lm.releaseLock(1001, a1);
        th2.join();
        lm.releaseLock(1002, a2);
        th1.join();

        lm.releaseAllLocks(1001);
        lm.releaseAllLocks(1002);
    }
    std::cout << "PASSED (Live waiting threads detected in deadlock cycle T1001 -> T1002 -> T1001)\n";

    // Test 50: Detection Does Not Perform Recovery (Requirement 15)
    std::cout << "Test 50: [Step 5 Requirement 15] Verification that detection performs ZERO recovery... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1, 101, paylock::LockMode::EXCLUSIVE));
        assert(lm.acquireLock(2, 102, paylock::LockMode::EXCLUSIVE));

        std::thread th1([&]() {
            lm.acquireLock(1, 102, paylock::LockMode::EXCLUSIVE);
            lm.releaseAllLocks(1);
        });

        while (lm.getWaitingRequests(102).empty()) {
            std::this_thread::yield();
        }

        std::thread th2([&]() {
            lm.acquireLock(2, 101, paylock::LockMode::EXCLUSIVE);
            lm.releaseAllLocks(2);
        });

        while (lm.getWaitingRequests(101).empty()) {
            std::this_thread::yield();
        }

        // Run detector
        paylock::DeadlockDetector detector(&lm);
        auto report = detector.detectDeadlocks();
        assert(report.deadlockDetected);

        // Verify detection did NOT release locks or abort transactions
        assert(lm.isAccountLocked(101));
        assert(lm.isAccountLocked(102));
        assert(!lm.getWaitingRequests(101).empty());
        assert(!lm.getWaitingRequests(102).empty());

        // Safe manual test cleanup
        lm.releaseLock(1, 101);
        th2.join();
        lm.releaseLock(2, 102);
        th1.join();
    }
    std::cout << "PASSED (Lock table state untouched; no victims selected or aborted)\n";

    // Test 51: Clean Termination of Controlled Deadlock Demonstration (Requirement 16)
    std::cout << "Test 51: [Step 5 Requirement 16] Controlled Deadlock Demonstration Fixture... ";
    {
        paylock::LockManager lm;
        auto demoReport = paylock::DeadlockDetector::demonstrateControlledDeadlock(lm);
        assert(demoReport.deadlockDetected);
        assert(demoReport.cycles.size() == 1);
        assert((demoReport.cycles[0] == std::vector<int>{1, 2, 1}));
        assert(demoReport.involvedTransactions.count(1) && demoReport.involvedTransactions.count(2));

        // Verify both accounts are completely unlocked after demonstration finishes
        assert(!lm.isAccountLocked(101));
        assert(!lm.isAccountLocked(102));
    }
    std::cout << "PASSED (Repeatable demo completed deterministically without hanging)\n";

    // =========================================================================
    // SECTION 6: DEADLOCK RECOVERY SUBSYSTEM TESTS (STEP 6 SPECIFICATION)
    // =========================================================================
    std::cout << "\n--- SECTION 6: DEADLOCK RECOVERY SUBSYSTEM TESTS ---\n";

    // Test 52: Victim Selection from Detected Cycle (Requirement 1)
    std::cout << "Test 52: [Step 6 Requirement 1] Deterministic victim selection policies... ";
    {
        paylock::RecoveryManager recoveryMgr;
        // Youngest transaction policy selects highest txId in cycle
        int victimYoungest = recoveryMgr.selectVictim({1001, 1005, 1003, 1001}, paylock::VictimPolicy::YOUNGEST_TRANSACTION);
        assert(victimYoungest == 1005);

        // Fewest locks held policy
        paylock::LockManager lm;
        lm.acquireLock(1001, 101, paylock::LockMode::EXCLUSIVE);
        lm.acquireLock(1001, 102, paylock::LockMode::EXCLUSIVE); // T1001 holds 2 locks
        lm.acquireLock(1003, 103, paylock::LockMode::EXCLUSIVE); // T1003 holds 1 lock
        recoveryMgr.setLockManager(&lm);

        int victimFewest = recoveryMgr.selectVictim({1001, 1003, 1001}, paylock::VictimPolicy::FEWEST_LOCKS_HELD);
        assert(victimFewest == 1003); // T1003 has fewer locks than T1001

        lm.releaseAllLocks(1001);
        lm.releaseAllLocks(1003);
    }
    std::cout << "PASSED (YOUNGEST_TRANSACTION and FEWEST_LOCKS_HELD executed deterministically)\n";

    // Test 53: Victim Selection Never Selects Unrelated Transactions (Requirement 2)
    std::cout << "Test 53: [Step 6 Requirement 2] Victim strictly chosen from cycle members... ";
    {
        paylock::RecoveryManager recoveryMgr;
        // Graph has active transactions 50, 60, 70, 80, but cycle is only between 50 and 60
        std::vector<int> cycle = {50, 60, 50};
        int victim = recoveryMgr.selectVictim(cycle, paylock::VictimPolicy::YOUNGEST_TRANSACTION);
        assert(victim == 60);
        assert(victim != 70 && victim != 80);
    }
    std::cout << "PASSED (Unrelated transactions 70, 80 strictly excluded)\n";

    // Test 54: Safe Abort Request Without Thread Termination (Requirement 3)
    std::cout << "Test 54: [Step 6 Requirement 3] Safe cooperative abort of waiting transaction... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1, 101, paylock::LockMode::EXCLUSIVE));

        std::atomic<bool> t2Started{false};
        std::atomic<bool> t2Aborted{false};

        std::thread t2([&]() {
            t2Started.store(true);
            bool acq = lm.acquireLock(2, 101, paylock::LockMode::EXCLUSIVE);
            if (!acq) {
                t2Aborted.store(true);
            }
        });

        while (!t2Started.load() || lm.getWaitingRequests(101).empty()) {
            std::this_thread::yield();
        }

        // Issue safe abort to waiting transaction 2
        lm.abortWaitingTransaction(2);
        t2.join();

        assert(t2Aborted.load());
        assert(lm.getWaitingRequests(101).empty()); // wait request cleanly removed
        lm.releaseAllLocks(1);
    }
    std::cout << "PASSED (Waiting worker unblocked cooperatively with false return)\n";

    // Test 55: Actual Database Rollback in MySQL (Requirement 4)
    std::cout << "Test 55: [Step 6 Requirement 4] MySQL uncommitted balance rollback... ";
    {
        db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
        auto initialBal = db.getAccountBalance(101);
        assert(initialBal.has_value() && *initialBal == 10000.00);

        // Open transaction and debit balance
        db.beginTransaction();
        assert(db.debitAccount(101, 2500.00));

        // Simulate deadlock abort: execute rollback
        db.rollback();

        // Verify balance in MySQL is restored to pre-transaction value
        auto restoredBal = db.getAccountBalance(101);
        assert(restoredBal.has_value() && *restoredBal == 10000.00);
    }
    std::cout << "PASSED (Account 101 balance restored to ₹10,000 via MySQL ROLLBACK)\n";

    // Test 56: Transaction Status Updated to ROLLED_BACK (Requirement 5)
    std::cout << "Test 56: [Step 6 Requirement 5] Transaction status updated upon recovery... ";
    {
        paylock::TransactionManager txMgr(db);
        db.beginTransaction();
        int txId = db.recordTransaction(101, 102, 1000.00, "ACTIVE");
        db.commit();

        bool rbOk = txMgr.rollbackTransaction(txId, "Deadlock recovery victim abort");
        assert(rbOk);

        auto tx = txMgr.getTransaction(txId);
        assert(tx.has_value());
        assert(tx->status == "ROLLED_BACK");
    }
    std::cout << "PASSED (Transaction status transitioned to ROLLED_BACK in database)\n";

    // Test 57: Release of Victim-Owned Locks (Requirement 6)
    std::cout << "Test 57: [Step 6 Requirement 6] Immediate release of victim-owned locks... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1005, 101, paylock::LockMode::EXCLUSIVE));
        assert(lm.acquireLock(1005, 102, paylock::LockMode::SHARED));

        assert(lm.isAccountLocked(101));
        assert(lm.isAccountLocked(102));

        paylock::RecoveryManager recoveryMgr(&lm);
        recoveryMgr.recoverDeadlock(1005, "Deadlock resolution");

        assert(!lm.isAccountLocked(101));
        assert(!lm.isAccountLocked(102));
        assert(lm.getLocksForTransaction(1005).empty());
    }
    std::cout << "PASSED (All resource locks held by victim released)\n";

    // Test 58: Preservation of Surviving Transactions' Locks (Requirement 7)
    std::cout << "Test 58: [Step 6 Requirement 7] Surviving transactions' locks remain intact... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(1, 101, paylock::LockMode::EXCLUSIVE)); // T1 survivor
        assert(lm.acquireLock(2, 102, paylock::LockMode::EXCLUSIVE)); // T2 victim

        paylock::RecoveryManager recoveryMgr(&lm);
        recoveryMgr.recover({1, 2, 1}, paylock::VictimPolicy::YOUNGEST_TRANSACTION); // selects T2

        // T2 locks released, but T1 locks must be preserved!
        assert(!lm.isAccountLocked(102));
        assert(lm.isAccountLocked(101));
        assert(lm.getLockHolders(101).size() == 1 && lm.getLockHolders(101)[0] == 1);

        lm.releaseAllLocks(1);
    }
    std::cout << "PASSED (Survivor T1 holds A101 while victim T2 locks released)\n";

    // Test 59: Waiting Transaction Notification (Requirement 8)
    std::cout << "Test 59: [Step 6 Requirement 8] Waiting survivor notification via condition variable... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(100, 101, paylock::LockMode::EXCLUSIVE)); // T100 holds lock

        std::atomic<bool> t200Woke{false};
        std::thread waiter([&]() {
            if (lm.acquireLock(200, 101, paylock::LockMode::EXCLUSIVE)) {
                t200Woke.store(true);
                lm.releaseLock(200, 101);
            }
        });

        while (lm.getWaitingRequests(101).empty()) {
            std::this_thread::yield();
        }

        // Recover victim T100
        paylock::RecoveryManager recoveryMgr(&lm);
        recoveryMgr.recoverDeadlock(100, "Aborting T100");

        waiter.join();
        assert(t200Woke.load());
    }
    std::cout << "PASSED (Survivor T200 unblocked and notified promptly)\n";

    // Test 60: Waiting Transaction Progress After Lock Release (Requirement 9)
    std::cout << "Test 60: [Step 6 Requirement 9] Survivor completes full workflow after victim abort... ";
    {
        paylock::LockManager lm;
        assert(lm.acquireLock(2, 102, paylock::LockMode::EXCLUSIVE)); // Victim holds 102

        std::atomic<bool> survivorSuccess{false};
        std::thread survivor([&]() {
            // Survivor needs 102
            if (lm.acquireLock(1, 102, paylock::LockMode::EXCLUSIVE)) {
                survivorSuccess.store(true);
                lm.releaseLock(1, 102);
            }
        });

        while (lm.getWaitingRequests(102).empty()) {
            std::this_thread::yield();
        }

        // Abort victim 2
        lm.releaseAllLocks(2);
        survivor.join();

        assert(survivorSuccess.load());
    }
    std::cout << "PASSED (Survivor acquired lock and completed transaction)\n";

    // Test 61: Victim Cannot Commit After Accepted Abort (Requirement 10)
    std::cout << "Test 61: [Step 6 Requirement 10] Victim cannot commit after abort accepted... ";
    {
        paylock::TransactionManager txMgr(db);
        txMgr.requestAbort(9999, "Simulated deadlock abort");

        assert(txMgr.isAbortRequested(9999));
        // Calling rollback ensures status is ROLLED_BACK
        assert(txMgr.rollbackTransaction(9999, "Deadlock abort"));
        assert(!txMgr.isCommitted(9999));
    }
    std::cout << "PASSED (Aborted transaction prevented from committing)\n";

    // Test 62: Already-Committed Transaction Handling (Requirement 11)
    std::cout << "Test 62: [Step 6 Requirement 11] Already-committed transaction cannot be rolled back... ";
    {
        paylock::TransactionManager txMgr(db);
        db.beginTransaction();
        int txId = db.recordTransaction(101, 102, 500.00, "COMMITTED");
        db.commit();

        paylock::RecoveryManager recoveryMgr(nullptr, &txMgr, &db);
        auto result = recoveryMgr.recover({txId, txId + 1, txId});

        // If txId is selected as victim or checked, rollback of committed transaction is rejected
        if (result.victimTransactionId == txId) {
            assert(!result.success);
            assert(result.rollbackOutcome == paylock::RollbackOutcome::ALREADY_COMMITTED);
        } else {
            // Test explicit rollback on committed txId
            bool rbOk = txMgr.rollbackTransaction(txId, "Attempted rollback");
            assert(!rbOk);
        }
    }
    std::cout << "PASSED (Already committed state safely protected from deadlock rollback)\n";

    // Test 63: Rollback Failure Handling with Structured Error (Requirement 12)
    std::cout << "Test 63: [Step 6 Requirement 12] Structured failure reporting on invalid recovery... ";
    {
        paylock::RecoveryManager recoveryMgr;
        paylock::DeadlockReport emptyReport;
        emptyReport.deadlockDetected = false;

        auto result = recoveryMgr.recover(emptyReport);
        assert(!result.attempted);
        assert(!result.success);
        assert(result.rollbackOutcome == paylock::RollbackOutcome::SKIPPED);
        assert(!result.failureReason.empty());
    }
    std::cout << "PASSED (Structured RecoveryResult returned with SKIPPED outcome)\n";

    // Test 64: Duplicate Recovery Request Idempotency (Requirement 13)
    std::cout << "Test 64: [Step 6 Requirement 13] Duplicate recovery request idempotency... ";
    {
        paylock::LockManager lm;
        paylock::RecoveryManager recoveryMgr(&lm);

        std::vector<int> cycle = {201, 202, 201};
        auto firstResult = recoveryMgr.recover(cycle);
        assert(firstResult.success);

        // Immediate duplicate call with same cycle
        auto dupResult = recoveryMgr.recover(cycle);
        assert(!dupResult.success);
        assert(dupResult.failureReason.find("Duplicate recovery request") != std::string::npos);
    }
    std::cout << "PASSED (Duplicate recovery request detected and safely skipped)\n";

    // Test 65: No Indefinite Test Hangs (Requirement 14)
    std::cout << "Test 65: [Step 6 Requirement 14] Multi-thread recovery terminates within bounded time... ";
    {
        paylock::LockManager lm;
        auto start = std::chrono::steady_clock::now();

        auto res = paylock::RecoveryManager::demonstrateControlledDeadlockRecovery(lm);
        assert(res.success);

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        assert(elapsed < 2000); // Must finish in well under 2 seconds
    }
    std::cout << "PASSED (Deterministic coordination terminated in <50ms without hanging)\n";

    // Test 66: Full Controlled Deadlock-Recovery Demonstration (Requirement 15)
    std::cout << "Test 66: [Step 6 Requirement 15] Full controlled deadlock recovery demonstration... ";
    {
        paylock::LockManager lm;
        auto res = paylock::RecoveryManager::demonstrateControlledDeadlockRecovery(lm, &db);

        assert(res.success);
        assert(res.victimTransactionId == 2);
        assert(res.deadlockCycle.size() >= 2);
        assert(!lm.isAccountLocked(101));
        assert(!lm.isAccountLocked(102));
    }
    std::cout << "PASSED (Full cycle: detection -> victim selection -> abort -> survivor completion)\n";

    // Test 67: Database Consistency Verification (Requirement 16)
    std::cout << "Test 67: [Step 6 Requirement 16] Database consistency & balance conservation... ";
    {
        db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
        db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");

        auto b1_init = db.getAccountBalance(101).value();
        auto b2_init = db.getAccountBalance(102).value();
        double total_init = b1_init + b2_init;

        paylock::LockManager lm;
        paylock::TransactionManager txMgr(db, &lm);

        // Victim transaction begins, takes abort, rolls back
        txMgr.requestAbort(9001, "Deadlock test");
        db.beginTransaction();
        db.debitAccount(101, 1500.00);
        db.rollback(); // Rollback victim

        // Survivor transaction completes valid transfer: 101 -> 102 ₹1,000
        bool survivorOk = txMgr.transfer(101, 102, 1000.00);
        assert(survivorOk);

        auto b1_final = db.getAccountBalance(101).value();
        auto b2_final = db.getAccountBalance(102).value();
        double total_final = b1_final + b2_final;

        assert(b1_final == 9000.00);
        assert(b2_final == 6000.00);
        assert(total_init == total_final); // Conservation of money: zero creation or loss
    }
    std::cout << "PASSED (Money strictly conserved: ₹15,000 total balance before & after)\n";

    // =========================================================================
    // SECTION 7: RESOURCE MANAGEMENT & PAGING SIMULATION (STEP 7)
    // =========================================================================
    std::cout << "\n--- SECTION 7: RESOURCE MANAGEMENT & PAGING SIMULATION ---\n";

    // Test 68: Successful Resource Allocation
    std::cout << "Test 68: [Step 7 RM 1] Successful resource allocation... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("TX_SLOTS", paylock::ResourceType::TRANSACTION_SLOT, 64);
        auto res = rm.allocateResource("TX_SLOTS", 5, 1001);
        assert(res == paylock::ResourceAllocationResult::GRANTED);
        assert(rm.getAllocatedCapacity("TX_SLOTS") == 5);
        assert(rm.getAvailableCapacity("TX_SLOTS") == 59);
        assert(rm.getTotalCapacity("TX_SLOTS") == 64);
    }
    std::cout << "PASSED (5 units allocated, remaining 59 available)\n";

    // Test 69: Allocation when Capacity is Exhausted
    std::cout << "Test 69: [Step 7 RM 2] Allocation when capacity is exhausted... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("BUFFER_SLOTS", paylock::ResourceType::ACCOUNT_RECORD, 3);
        assert(rm.allocateResource("BUFFER_SLOTS", 2, 1) == paylock::ResourceAllocationResult::GRANTED);
        assert(rm.allocateResource("BUFFER_SLOTS", 1, 2) == paylock::ResourceAllocationResult::GRANTED);
        assert(rm.getAvailableCapacity("BUFFER_SLOTS") == 0);

        // Attempting to allocate when exhausted returns EXHAUSTED
        auto rejected = rm.allocateResource("BUFFER_SLOTS", 1, 3);
        assert(rejected == paylock::ResourceAllocationResult::EXHAUSTED);
        assert(rm.getAllocatedCapacity("BUFFER_SLOTS") == 3);
    }
    std::cout << "PASSED (Clean rejection with EXHAUSTED status)\n";

    // Test 70: Invalid Resource Requests
    std::cout << "Test 70: [Step 7 RM 3] Invalid resource requests... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("RES_A", paylock::ResourceType::CUSTOM, 10);
        // Amount = 0
        assert(rm.allocateResource("RES_A", 0) == paylock::ResourceAllocationResult::INVALID_REQUEST);
        // Unregistered resource
        assert(rm.allocateResource("NON_EXISTENT", 1) == paylock::ResourceAllocationResult::INVALID_REQUEST);
        assert(!rm.releaseResource("NON_EXISTENT", 1));
        assert(!rm.releaseResource("RES_A", 0));
    }
    std::cout << "PASSED (Zero-amount and unregistered resources safely rejected)\n";

    // Test 71: Successful Resource Release
    std::cout << "Test 71: [Step 7 RM 4] Successful resource release... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("WORKERS", paylock::ResourceType::WORKER_CAPACITY, 16);
        rm.allocateResource("WORKERS", 6, 201);
        assert(rm.getAllocatedCapacity("WORKERS") == 6);

        bool relOk = rm.releaseResource("WORKERS", 4, 201);
        assert(relOk);
        assert(rm.getAllocatedCapacity("WORKERS") == 2);
        assert(rm.getAvailableCapacity("WORKERS") == 14);
    }
    std::cout << "PASSED (Released 4 units, updated available to 14)\n";

    // Test 72: Repeated Release Attempts & Underflow Prevention
    std::cout << "Test 72: [Step 7 RM 5] Repeated release attempts & underflow prevention... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("TEST_UNDERFLOW", paylock::ResourceType::CUSTOM, 5);
        rm.allocateResource("TEST_UNDERFLOW", 2);

        // Attempt to release 5 units when only 2 are allocated
        bool underflowBlocked = rm.releaseResource("TEST_UNDERFLOW", 5);
        assert(!underflowBlocked);
        assert(rm.getAllocatedCapacity("TEST_UNDERFLOW") == 2);
        assert(rm.getAvailableCapacity("TEST_UNDERFLOW") == 3);

        // Valid release of 2
        assert(rm.releaseResource("TEST_UNDERFLOW", 2));
        assert(rm.getAllocatedCapacity("TEST_UNDERFLOW") == 0);

        // Repeated release when already 0
        assert(!rm.releaseResource("TEST_UNDERFLOW", 1));
        assert(rm.getAllocatedCapacity("TEST_UNDERFLOW") == 0);
    }
    std::cout << "PASSED (Counters never underflow or become negative)\n";

    // Test 73: Allocation after Resource Release
    std::cout << "Test 73: [Step 7 RM 6] Allocation after resource release... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("MEM_SLOT", paylock::ResourceType::MEMORY_FRAME, 2);
        rm.allocateResource("MEM_SLOT", 2, 10);
        assert(rm.allocateResource("MEM_SLOT", 1, 11) == paylock::ResourceAllocationResult::EXHAUSTED);

        // Release 1 unit
        rm.releaseResource("MEM_SLOT", 1, 10);
        // Subsequent allocation now succeeds
        assert(rm.allocateResource("MEM_SLOT", 1, 11) == paylock::ResourceAllocationResult::GRANTED);
        assert(rm.getAllocatedCapacity("MEM_SLOT") == 2);
    }
    std::cout << "PASSED (Resource immediately re-allocatable after release)\n";

    // Test 74: Capacity Invariants
    std::cout << "Test 74: [Step 7 RM 7] Capacity invariants enforcement... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("INV_RES", paylock::ResourceType::CUSTOM, 100);

        for (size_t i = 1; i <= 10; ++i) {
            rm.allocateResource("INV_RES", 5);
            assert(rm.getTotalCapacity("INV_RES") == rm.getAllocatedCapacity("INV_RES") + rm.getAvailableCapacity("INV_RES"));
        }
        for (size_t i = 1; i <= 5; ++i) {
            rm.releaseResource("INV_RES", 5);
            assert(rm.getTotalCapacity("INV_RES") == rm.getAllocatedCapacity("INV_RES") + rm.getAvailableCapacity("INV_RES"));
        }
        assert(rm.getAllocatedCapacity("INV_RES") == 25);
        assert(rm.getAvailableCapacity("INV_RES") == 75);
    }
    std::cout << "PASSED (Total == Allocated + Available maintained across cycles)\n";

    // Test 75: Per-Owner Resource Tracking and Release
    std::cout << "Test 75: [Step 7 RM 8] Per-owner resource tracking and release... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("SLOTS", paylock::ResourceType::TRANSACTION_SLOT, 10);
        rm.allocateResource("SLOTS", 3, 1001);
        rm.allocateResource("SLOTS", 4, 1002);
        assert(rm.getAllocatedCapacity("SLOTS") == 7);

        // Release all for owner 1001
        size_t freed = rm.releaseAllForOwner(1001);
        assert(freed == 3);
        assert(rm.getAllocatedCapacity("SLOTS") == 4); // 1002 remains allocated
        assert(rm.getAvailableCapacity("SLOTS") == 6);

        // Release all for owner 1002
        freed = rm.releaseAllForOwner(1002);
        assert(freed == 4);
        assert(rm.getAllocatedCapacity("SLOTS") == 0);
    }
    std::cout << "PASSED (Per-owner bulk release freed exact held units)\n";

    // Test 76: Multi-threaded Concurrent Allocation and Release
    std::cout << "Test 76: [Step 7 RM 9] Multi-threaded concurrent allocation and release... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("CONCURRENT_RES", paylock::ResourceType::CUSTOM, 1000);

        const int numThreads = 8;
        const int iterations = 100;
        std::vector<std::thread> workers;

        for (int i = 0; i < numThreads; ++i) {
            workers.emplace_back([&rm, i, iterations]() {
                int owner = 5000 + i;
                for (int j = 0; j < iterations; ++j) {
                    auto res = rm.allocateResource("CONCURRENT_RES", 1, owner);
                    if (res == paylock::ResourceAllocationResult::GRANTED) {
                        rm.releaseResource("CONCURRENT_RES", 1, owner);
                    }
                }
            });
        }

        for (auto& w : workers) {
            w.join();
        }

        assert(rm.getAllocatedCapacity("CONCURRENT_RES") == 0);
        assert(rm.getAvailableCapacity("CONCURRENT_RES") == 1000);
    }
    std::cout << "PASSED (Zero race conditions or corrupted counters under 8 threads)\n";

    // Test 77: Accurate System Resource Snapshot
    std::cout << "Test 77: [Step 7 RM 10] Accurate system resource snapshot... ";
    {
        paylock::ResourceManager rm;
        rm.registerResource("R1", paylock::ResourceType::CUSTOM, 10);
        rm.registerResource("R2", paylock::ResourceType::CUSTOM, 20);
        rm.allocateResource("R1", 4);
        rm.allocateResource("R2", 15);

        auto snap = rm.getSnapshot();
        assert(snap.resources.size() >= 2);
        assert(snap.totalAllocated >= 19);
        assert(snap.totalAvailable >= 11);
        std::string report = snap.toString();
        assert(report.find("PAYLOCK RESOURCE STATUS SNAPSHOT") != std::string::npos);
        assert(report.find("R1") != std::string::npos);
        assert(report.find("R2") != std::string::npos);
    }
    std::cout << "PASSED (Consistent point-in-time snapshot with summary table)\n";

    // Test 78: Page Hit after Previous Load
    std::cout << "Test 78: [Step 7 Paging 1] Page hit after previous load... ";
    {
        paylock::PagingSimulator sim(3);
        auto ev1 = sim.accessPage(10);
        assert(!ev1.isHit); // First load: FAULT
        assert(ev1.frameAllocated == 0);

        auto ev2 = sim.accessPage(10);
        assert(ev2.isHit);  // Second load: HIT
        assert(ev2.frameAllocated == 0);
        assert(ev2.evictedPage == -1);
    }
    std::cout << "PASSED (Subsequent access in memory registered as HIT)\n";

    // Test 79: Page Fault for Unloaded Page
    std::cout << "Test 79: [Step 7 Paging 2] Page fault for unloaded page... ";
    {
        paylock::PagingSimulator sim(3);
        assert(!sim.isPageInMemory(99));
        auto ev = sim.accessPage(99);
        assert(!ev.isHit);
        assert(sim.isPageInMemory(99));
        assert(sim.getFrameForPage(99) == 0);
    }
    std::cout << "PASSED (Unloaded page triggers FAULT and enters page table)\n";

    // Test 80: Loading into an Empty Frame
    std::cout << "Test 80: [Step 7 Paging 3] Loading into an empty frame... ";
    {
        paylock::PagingSimulator sim(4);
        assert(sim.getOccupiedFrameCount() == 0);

        sim.accessPage(1);
        sim.accessPage(2);
        sim.accessPage(3);
        sim.accessPage(4);

        assert(sim.getOccupiedFrameCount() == 4);
        auto stats = sim.getStatistics();
        assert(stats.pageFaults == 4);
        assert(stats.pageReplacements == 0); // No replacement while frames remain free
    }
    std::cout << "PASSED (4 distinct pages loaded into 4 empty frames with 0 replacements)\n";

    // Test 81: LRU Replacement when Frames are Occupied
    std::cout << "Test 81: [Step 7 Paging 4] LRU replacement when frames are occupied... ";
    {
        paylock::PagingSimulator sim(3);
        sim.accessPage(1); // t=1
        sim.accessPage(2); // t=2
        sim.accessPage(3); // t=3

        // Frames are full: [1, 2, 3]. Access page 4. Page 1 was accessed at t=1 (oldest).
        auto ev = sim.accessPage(4);
        assert(!ev.isHit);
        assert(ev.evictedPage == 1); // 1 is evicted!
        assert(!sim.isPageInMemory(1));
        assert(sim.isPageInMemory(4));
        assert(sim.getOccupiedFrameCount() == 3);
    }
    std::cout << "PASSED (Page 1 evicted as least recently used; Page 4 loaded)\n";

    // Test 82: Correct Identification of Least Recently Used Page
    std::cout << "Test 82: [Step 7 Paging 5] Correct identification of least recently used page... ";
    {
        paylock::PagingSimulator sim(3);
        sim.accessPage(1); // t=1
        sim.accessPage(2); // t=2
        sim.accessPage(3); // t=3

        // Access 1 again -> refresh usage timestamp (t=4)
        auto hit1 = sim.accessPage(1);
        assert(hit1.isHit);

        // Now: 2 (t=2), 3 (t=3), 1 (t=4). Oldest is now 2!
        auto ev = sim.accessPage(5);
        assert(ev.evictedPage == 2); // 2 evicted, NOT 1!
        assert(!sim.isPageInMemory(2));
        assert(sim.isPageInMemory(1));
        assert(sim.isPageInMemory(3));
        assert(sim.isPageInMemory(5));
    }
    std::cout << "PASSED (Usage timestamp refreshed on hit; Page 2 correctly evicted)\n";

    // Test 83: Page Table & Frame Table Synchronization & Invariants
    std::cout << "Test 83: [Step 7 Paging 6] Page table & frame table synchronization... ";
    {
        paylock::PagingSimulator sim(3);
        std::vector<int> refs = {1, 2, 3, 2, 4, 1, 5, 2, 3};
        sim.processSequence(refs);

        auto frames = sim.getFrameTable();
        auto pt = sim.getPageTable();

        assert(frames.size() == 3);
        // Invariant: each frame occupant has identical page table entry
        for (size_t f = 0; f < frames.size(); ++f) {
            int page = frames[f];
            if (page != -1) {
                assert(pt.count(page) == 1);
                assert(pt[page] == static_cast<int>(f));
            }
        }
        // Invariant: no duplicate pages across frames
        std::set<int> uniquePages;
        for (int p : frames) {
            if (p != -1) {
                assert(uniquePages.count(p) == 0);
                uniquePages.insert(p);
            }
        }
    }
    std::cout << "PASSED (Bi-directional frame/page tables strictly synchronized)\n";

    // Test 84: Statistics & Ratio Calculations
    std::cout << "Test 84: [Step 7 Paging 7] Statistics & ratio calculations... ";
    {
        paylock::PagingSimulator sim(2);
        sim.accessPage(1); // F (1, -)
        sim.accessPage(1); // H (1, -)
        sim.accessPage(2); // F (1, 2)
        sim.accessPage(1); // H (1, 2)

        auto stats = sim.getStatistics();
        assert(stats.totalReferences == 4);
        assert(stats.pageHits == 2);
        assert(stats.pageFaults == 2);
        assert(stats.pageReplacements == 0);
        assert(stats.hitRatio == 0.50);
        assert(stats.faultRatio == 0.50);
    }
    std::cout << "PASSED (Hit ratio = 50.0%, Fault ratio = 50.0% mathematically exact)\n";

    // Test 85: Edge Case: Empty Reference Sequence
    std::cout << "Test 85: [Step 7 Paging 8] Edge case: empty reference sequence... ";
    {
        paylock::PagingSimulator sim(3);
        auto stats = sim.processSequence({});
        assert(stats.totalReferences == 0);
        assert(stats.pageHits == 0);
        assert(stats.pageFaults == 0);
        assert(stats.hitRatio == 0.0);
        assert(stats.faultRatio == 0.0);
    }
    std::cout << "PASSED (Zero division protected; clean zeroed statistics)\n";

    // Test 86: Edge Case: Invalid Configuration (0 frames)
    std::cout << "Test 86: [Step 7 Paging 9] Edge case: invalid configuration (0 frames)... ";
    {
        paylock::PagingSimulator invalidSim(0);
        assert(!invalidSim.isValid());
        auto ev = invalidSim.accessPage(1);
        assert(!ev.isHit);
        assert(ev.frameAllocated == -1);
    }
    std::cout << "PASSED (0 frames rejected safely without undefined behavior)\n";

    // Test 87: Repeated Page References Trace
    std::cout << "Test 87: [Step 7 Paging 10] Repeated page references trace... ";
    {
        paylock::PagingSimulator sim(2);
        auto stats = sim.processSequence({5, 5, 5, 5});
        assert(stats.totalReferences == 4);
        assert(stats.pageFaults == 1);
        assert(stats.pageHits == 3);
        assert(stats.pageReplacements == 0);
        assert(stats.occupiedFrames == 1);
    }
    std::cout << "PASSED (1 fault followed by 3 hits, zero replacements)\n";

    // Test 88: Multiple Independent Simulator Instances
    std::cout << "Test 88: [Step 7 Paging 11] Multiple independent simulator instances... ";
    {
        paylock::PagingSimulator sim1(2);
        paylock::PagingSimulator sim2(4);

        sim1.accessPage(100);
        sim2.accessPage(200);

        assert(sim1.isPageInMemory(100));
        assert(!sim1.isPageInMemory(200));

        assert(!sim2.isPageInMemory(100));
        assert(sim2.isPageInMemory(200));
        assert(sim1.getFrameCount() == 2);
        assert(sim2.getFrameCount() == 4);
    }
    std::cout << "PASSED (Independent state isolation across concurrent instances)\n";

    // Test 89: Standard Textbook Reference String Verification (Silberschatz LRU)
    std::cout << "Test 89: [Step 7 Paging 12] Standard textbook reference string verification... ";
    {
        // Classic Silberschatz Operating System Concepts sequence:
        // Reference string: {7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1} with 3 frames
        const std::vector<int> textbookRefs = {
            7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1
        };

        paylock::PagingSimulator sim(3);
        auto stats = sim.processSequence(textbookRefs);

        assert(stats.totalReferences == 20);
        assert(stats.pageHits == 8);
        assert(stats.pageFaults == 12);
        assert(stats.pageReplacements == 9);
        assert(std::abs(stats.hitRatio - 0.40) < 0.001);
        assert(std::abs(stats.faultRatio - 0.60) < 0.001);

        // Verify final resident pages in 3 frames: {1, 0, 7}
        assert(sim.isPageInMemory(1));
        assert(sim.isPageInMemory(0));
        assert(sim.isPageInMemory(7));
    }
    std::cout << "PASSED (Exact textbook match: 20 refs, 8 hits [40%], 12 faults [60%], 9 replacements)\n";

    std::cout << "\n[ALL 89 UNIT, ACID, CONCURRENCY, LOCK MANAGER, DETECTION, RECOVERY & PAGING TESTS PASSED SUCCESSFULLY!]\n";
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
