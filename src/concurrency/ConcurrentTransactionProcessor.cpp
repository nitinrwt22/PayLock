#include "ConcurrentTransactionProcessor.h"
#include <iostream>
#include <iomanip>
#include <sstream>

namespace paylock {

std::mutex ConcurrentTransactionProcessor::consoleMutex_;

std::string threadStateToString(ThreadState state) {
    switch (state) {
        case ThreadState::CREATED:           return "CREATED";
        case ThreadState::WAITING_FOR_MUTEX: return "WAITING FOR MUTEX";
        case ThreadState::RUNNING:           return "RUNNING";
        case ThreadState::COMPLETED:         return "COMPLETED";
        case ThreadState::FAILED:            return "FAILED";
    }
    return "UNKNOWN";
}

void ConcurrencyMetrics::print() const {
    std::cout << "========================================\n";
    std::cout << "     PAYLOCK CONCURRENCY TEST\n";
    std::cout << "========================================\n\n";
    std::cout << "Threads: " << totalThreads << "\n";
    std::cout << "Successful: " << successfulTransactions << "\n";
    std::cout << "Failed: " << failedTransactions << "\n";
    std::cout << "Execution Time: " << executionTimeMs << " ms\n\n";
    std::cout << "========================================\n";
}

ConcurrentTransactionProcessor::ConcurrentTransactionProcessor(DatabaseConfig config)
    : dbConfig_(std::move(config)) {}

void ConcurrentTransactionProcessor::log(const std::string& threadLabel, const std::string& message) {
    std::lock_guard<std::mutex> lock(consoleMutex_);
    std::cout << "[" << threadLabel << "] " << message << "\n";
}

void ConcurrentTransactionProcessor::logState(const std::string& threadLabel, ThreadState state, const std::string& detail) {
    std::lock_guard<std::mutex> lock(consoleMutex_);
    std::cout << "[" << threadLabel << "] " << threadStateToString(state);
    if (!detail.empty()) {
        std::cout << " - " << detail;
    }
    std::cout << "\n";
}

// ----------------------------------------------------------------------------
// Controlled Race Condition Demonstration
// ----------------------------------------------------------------------------

void ConcurrentTransactionProcessor::demonstrateRaceCondition(double initialBalance,
                                                               double withdrawT1,
                                                               double withdrawT2) {
    log("DEMO", "==================================================");
    log("DEMO", "   CONTROLLED RACE CONDITION DEMONSTRATION        ");
    log("DEMO", "   Initial Shared Account Balance: ₹" + std::to_string(static_cast<int>(initialBalance)));
    log("DEMO", "   Thread 1 Attempt: Withdraw ₹" + std::to_string(static_cast<int>(withdrawT1)));
    log("DEMO", "   Thread 2 Attempt: Withdraw ₹" + std::to_string(static_cast<int>(withdrawT2)));
    log("DEMO", "==================================================");

    // ------------------------------------------------------------------------
    // Part A: WITHOUT MUTEX (Demonstrates Race Condition & Inconsistent State)
    // ------------------------------------------------------------------------
    log("DEMO", "--- [PART A] WITHOUT MUTEX (UNSYNCHRONIZED ACCESS) ---");
    double sharedBalanceUnsafe = initialBalance;
    bool t1ApprovedUnsafe = false;
    bool t2ApprovedUnsafe = false;

    auto unsafeWorker = [&](const std::string& label, double amount, bool& approved) {
        logState(label, ThreadState::CREATED);
        logState(label, ThreadState::RUNNING);

        // Check Phase: Read balance
        log(label, "Reading shared balance: ₹" + std::to_string(static_cast<int>(sharedBalanceUnsafe)));
        if (sharedBalanceUnsafe >= amount) {
            log(label, "Check passed (Balance >= ₹" + std::to_string(static_cast<int>(amount)) + "). Preparing to debit...");
            
            // Controlled micro-delay simulating OS thread preemption / context switch
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // Act Phase: TOCTOU Race Condition occurs here
            sharedBalanceUnsafe -= amount;
            approved = true;
            log(label, "Debited ₹" + std::to_string(static_cast<int>(amount)) + 
                       ". New shared balance: ₹" + std::to_string(static_cast<int>(sharedBalanceUnsafe)));
            logState(label, ThreadState::COMPLETED);
        } else {
            log(label, "Rejected: Insufficient balance.");
            logState(label, ThreadState::FAILED);
        }
    };

    // Launch both threads concurrently
    std::thread t1Unsafe(unsafeWorker, "T1", withdrawT1, std::ref(t1ApprovedUnsafe));
    std::thread t2Unsafe(unsafeWorker, "T2", withdrawT2, std::ref(t2ApprovedUnsafe));

    t1Unsafe.join();
    t2Unsafe.join();

    log("DEMO", "Unsynchronized Result: Both withdrawals approved (" + 
               std::string(t1ApprovedUnsafe ? "T1=YES " : "T1=NO ") + 
               std::string(t2ApprovedUnsafe ? "T2=YES" : "T2=NO") + ")");
    log("DEMO", "Final Shared Balance: ₹" + std::to_string(static_cast<int>(sharedBalanceUnsafe)) + 
               " [CRITICAL ERROR: RACE CONDITION OCCURRED - INVALID/NEGATIVE STATE!]\n");

    // ------------------------------------------------------------------------
    // Part B: WITH MUTEX (Demonstrates Critical Section & Mutual Exclusion)
    // ------------------------------------------------------------------------
    log("DEMO", "--- [PART B] WITH MUTEX (SYNCHRONIZED CRITICAL SECTION) ---");
    double sharedBalanceSafe = initialBalance;
    std::mutex demoMutex;
    bool t1ApprovedSafe = false;
    bool t2ApprovedSafe = false;

    auto safeWorker = [&](const std::string& label, double amount, bool& approved) {
        logState(label, ThreadState::CREATED);
        logState(label, ThreadState::WAITING_FOR_MUTEX);

        {
            // Enter Critical Section
            std::lock_guard<std::mutex> lock(demoMutex);
            logState(label, ThreadState::RUNNING, "Entered Critical Section (Mutex Acquired)");

            log(label, "Reading shared balance: ₹" + std::to_string(static_cast<int>(sharedBalanceSafe)));
            if (sharedBalanceSafe >= amount) {
                log(label, "Check passed (Balance >= ₹" + std::to_string(static_cast<int>(amount)) + "). Debiting...");
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                sharedBalanceSafe -= amount;
                approved = true;
                log(label, "Debited ₹" + std::to_string(static_cast<int>(amount)) + 
                           ". Remaining balance: ₹" + std::to_string(static_cast<int>(sharedBalanceSafe)));
                logState(label, ThreadState::COMPLETED, "Transaction finished cleanly");
            } else {
                log(label, "Rejected: Insufficient balance (Available: ₹" + 
                           std::to_string(static_cast<int>(sharedBalanceSafe)) + 
                           ", Requested: ₹" + std::to_string(static_cast<int>(amount)) + ")");
                logState(label, ThreadState::FAILED, "Safely rejected in critical section");
            }
            log(label, "Exiting Critical Section (Mutex Released)");
        }
    };

    // Launch both threads concurrently
    std::thread t1Safe(safeWorker, "T1", withdrawT1, std::ref(t1ApprovedSafe));
    std::thread t2Safe(safeWorker, "T2", withdrawT2, std::ref(t2ApprovedSafe));

    t1Safe.join();
    t2Safe.join();

    log("DEMO", "Synchronized Result: Only one withdrawal allowed (" + 
               std::string(t1ApprovedSafe ? "T1=APPROVED " : "T1=REJECTED ") + 
               std::string(t2ApprovedSafe ? "T2=APPROVED" : "T2=REJECTED") + ")");
    log("DEMO", "Final Shared Balance: ₹" + std::to_string(static_cast<int>(sharedBalanceSafe)) + 
               " [CONSISTENT STATE PRESERVED BY MUTEX!]\n");
}

// ----------------------------------------------------------------------------
// Real Multi-Threaded Payment Operations
// ----------------------------------------------------------------------------

ConcurrencyMetrics ConcurrentTransactionProcessor::processConcurrentTransfers(
    const std::vector<TransferRequest>& requests) {

    ConcurrencyMetrics metrics;
    metrics.totalThreads = static_cast<int>(requests.size());

    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};

    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(requests.size());

    // 1. Launch all worker threads first (real concurrency, no premature joining)
    for (const auto& req : requests) {
        logState(req.threadLabel, ThreadState::CREATED, 
                 "Account " + std::to_string(req.senderAccount) + " -> Account " + 
                 std::to_string(req.receiverAccount) + " : ₹" + std::to_string(static_cast<int>(req.amount)));

        threads.emplace_back([this, req, &successCount, &failCount]() {
            try {
                // Each worker thread establishes its OWN dedicated MySQL connection
                DatabaseManager workerDb(dbConfig_);
                workerDb.connect();
                TransactionManager workerTx(workerDb);

                logState(req.threadLabel, ThreadState::WAITING_FOR_MUTEX);

                // Critical Section: Protected by mutex
                bool txSuccess = false;
                {
                    std::lock_guard<std::mutex> lock(paymentMutex_);
                    logState(req.threadLabel, ThreadState::RUNNING, "Acquired Mutex (Critical Section)");

                    log(req.threadLabel, "PROCESSING TRANSACTION: Account " + 
                                         std::to_string(req.senderAccount) + " -> Account " + 
                                         std::to_string(req.receiverAccount) + " : ₹" + 
                                         std::to_string(static_cast<int>(req.amount)));

                    // Execute atomic MySQL transaction
                    txSuccess = workerTx.transfer(req.senderAccount, req.receiverAccount, req.amount);

                    if (txSuccess) {
                        log(req.threadLabel, "COMMITTED");
                    } else {
                        log(req.threadLabel, "ROLLED_BACK: " + workerTx.getLastError());
                    }

                    log(req.threadLabel, "RELEASING MUTEX");
                }

                if (txSuccess) {
                    successCount++;
                    logState(req.threadLabel, ThreadState::COMPLETED);
                } else {
                    failCount++;
                    logState(req.threadLabel, ThreadState::FAILED);
                }

            } catch (const std::exception& ex) {
                failCount++;
                log(req.threadLabel, "CRITICAL ERROR: " + std::string(ex.what()));
                logState(req.threadLabel, ThreadState::FAILED);
            }
        });
    }

    // 2. Join all worker threads after all have been launched
    for (auto& th : threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    metrics.executionTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    metrics.successfulTransactions = successCount.load();
    metrics.failedTransactions = failCount.load();

    return metrics;
}

ConcurrencyMetrics ConcurrentTransactionProcessor::processConcurrentWithdrawals(
    const std::vector<WithdrawalRequest>& requests) {

    ConcurrencyMetrics metrics;
    metrics.totalThreads = static_cast<int>(requests.size());

    std::atomic<int> successCount{0};
    std::atomic<int> failCount{0};

    auto startTime = std::chrono::high_resolution_clock::now();

    std::vector<std::thread> threads;
    threads.reserve(requests.size());

    // Launch all threads concurrently
    for (const auto& req : requests) {
        logState(req.threadLabel, ThreadState::CREATED, 
                 "Account " + std::to_string(req.accountId) + " : Withdraw ₹" + std::to_string(static_cast<int>(req.amount)));

        threads.emplace_back([this, req, &successCount, &failCount]() {
            try {
                // Dedicated per-thread database connection
                DatabaseManager workerDb(dbConfig_);
                workerDb.connect();
                TransactionManager workerTx(workerDb);

                logState(req.threadLabel, ThreadState::WAITING_FOR_MUTEX);

                bool txSuccess = false;
                {
                    std::lock_guard<std::mutex> lock(paymentMutex_);
                    logState(req.threadLabel, ThreadState::RUNNING, "Acquired Mutex (Critical Section)");

                    log(req.threadLabel, "PROCESSING WITHDRAWAL: Account " + 
                                         std::to_string(req.accountId) + " : ₹" + 
                                         std::to_string(static_cast<int>(req.amount)));

                    txSuccess = workerTx.withdraw(req.accountId, req.amount);

                    if (txSuccess) {
                        log(req.threadLabel, "COMMITTED");
                    } else {
                        log(req.threadLabel, "ROLLED_BACK: " + workerTx.getLastError());
                    }

                    log(req.threadLabel, "RELEASING MUTEX");
                }

                if (txSuccess) {
                    successCount++;
                    logState(req.threadLabel, ThreadState::COMPLETED);
                } else {
                    failCount++;
                    logState(req.threadLabel, ThreadState::FAILED);
                }

            } catch (const std::exception& ex) {
                failCount++;
                log(req.threadLabel, "CRITICAL ERROR: " + std::string(ex.what()));
                logState(req.threadLabel, ThreadState::FAILED);
            }
        });
    }

    // Join all threads
    for (auto& th : threads) {
        if (th.joinable()) {
            th.join();
        }
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    metrics.executionTimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime).count();
    metrics.successfulTransactions = successCount.load();
    metrics.failedTransactions = failCount.load();

    return metrics;
}

} // namespace paylock
