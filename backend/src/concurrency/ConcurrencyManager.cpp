#include "concurrency/ConcurrencyManager.h"
#include "logging/Logger.h"
#include <iostream>
#include <iomanip>
#include <algorithm>

namespace paylock {

ConcurrencyManager::ConcurrencyManager(DatabaseConfig config)
    : dbConfig_(std::move(config)), processor_(dbConfig_) {}

ConcurrencyManager::~ConcurrencyManager() {
    shutdown();
}

void ConcurrencyManager::start(size_t numWorkers) {
    if (isRunning_.load()) {
        return;
    }

    stopRequested_ = false;
    isRunning_ = true;
    workers_.clear();
    workers_.reserve(numWorkers);

    {
        std::lock_guard<std::mutex> lock(registryMutex_);
        workerRegistry_.clear();
    }

    Logger::info("Starting ConcurrencyManager with " + std::to_string(numWorkers) + " worker threads", "ConcurrencyManager");

    for (size_t i = 1; i <= numWorkers; ++i) {
        std::string workerId = "W" + (i < 10 ? std::string("00") : (i < 100 ? std::string("0") : std::string(""))) + std::to_string(i);

        {
            std::lock_guard<std::mutex> lock(registryMutex_);
            workerRegistry_[workerId] = {workerId, ThreadState::CREATED, "Idle / Initialized", 0};
        }

        workers_.emplace_back(&ConcurrencyManager::workerLoop, this, workerId);
    }
}

void ConcurrencyManager::workerLoop(const std::string& workerId) {
    // Each worker thread establishes its OWN dedicated MySQL connection
    DatabaseManager workerDb(dbConfig_);
    try {
        workerDb.connect();
    } catch (const std::exception& ex) {
        Logger::error("Worker " + workerId + " failed to connect to MySQL: " + ex.what(), "ConcurrencyManager");
        updateWorkerState(workerId, ThreadState::FAILED, "Connection error: " + std::string(ex.what()));
        return;
    }

    TransactionManager workerTx(workerDb);

    while (true) {
        TransactionJob job;

        {
            std::unique_lock<std::mutex> lock(queueMutex_);
            updateWorkerState(workerId, ThreadState::WAITING, "Waiting for job in queue");

            taskCv_.wait(lock, [this]() {
                return stopRequested_.load() || !jobQueue_.empty();
            });

            if (stopRequested_.load() && jobQueue_.empty()) {
                updateWorkerState(workerId, ThreadState::COMPLETED, "Worker terminated cleanly");
                break;
            }

            job = std::move(jobQueue_.front());
            jobQueue_.pop();

            statActiveThreads_++;
            int currentActive = statActiveThreads_.load();
            int currentPeak = statPeakThreads_.load();
            while (currentActive > currentPeak && !statPeakThreads_.compare_exchange_weak(currentPeak, currentActive));
        }

        // Running Phase
        std::string taskDesc = job.jobId + " : ";
        if (job.type == TransactionJobType::TRANSFER) {
            taskDesc += "Transfer A" + std::to_string(job.senderAccount) + " -> A" + 
                        std::to_string(job.receiverAccount) + " (₹" + std::to_string(static_cast<int>(job.amount)) + ")";
        } else if (job.type == TransactionJobType::DEPOSIT) {
            taskDesc += "Deposit -> A" + std::to_string(job.receiverAccount) + " (₹" + std::to_string(static_cast<int>(job.amount)) + ")";
        } else {
            taskDesc += "Withdraw <- A" + std::to_string(job.senderAccount) + " (₹" + std::to_string(static_cast<int>(job.amount)) + ")";
        }

        updateWorkerState(workerId, ThreadState::RUNNING, taskDesc);
        Logger::info("[Thread " + workerId + "] Transaction " + job.jobId + " started", "ConcurrencyManager");

        bool success = false;
        std::string error;

        // Critical Section protecting account balance modifications across concurrent threads
        {
            std::lock_guard<std::mutex> csLock(criticalSectionMutex_);
            try {
                if (job.type == TransactionJobType::TRANSFER) {
                    success = workerTx.transfer(job.senderAccount, job.receiverAccount, job.amount);
                } else if (job.type == TransactionJobType::DEPOSIT) {
                    success = workerTx.deposit(job.receiverAccount, job.amount);
                } else {
                    success = workerTx.withdraw(job.senderAccount, job.amount);
                }

                if (!success) {
                    error = workerTx.getLastError();
                }
            } catch (const std::exception& ex) {
                success = false;
                error = ex.what();
            }
        }

        if (success) {
            statCompletedJobs_++;
            Logger::info("[Thread " + workerId + "] Transaction " + job.jobId + " committed", "ConcurrencyManager");
        } else {
            statFailedJobs_++;
            Logger::warn("[Thread " + workerId + "] Transaction " + job.jobId + " failed: " + error, "ConcurrencyManager");
        }

        if (job.onComplete) {
            try {
                job.onComplete(success, error);
            } catch (...) {}
        }

        // Completion & bookkeeping
        statActiveThreads_--;
        {
            std::lock_guard<std::mutex> lock(registryMutex_);
            auto it = workerRegistry_.find(workerId);
            if (it != workerRegistry_.end()) {
                it->second.state = success ? ThreadState::COMPLETED : ThreadState::FAILED;
                it->second.currentTask = taskDesc + (success ? " [COMMITTED]" : " [FAILED]");
                it->second.completedCount++;
            }
        }

        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            if (jobQueue_.empty() && statActiveThreads_.load() == 0) {
                completionCv_.notify_all();
            }
        }
    }
}

void ConcurrencyManager::submitTransaction(const TransactionJob& job) {
    if (!isRunning_.load()) {
        start();
    }

    {
        std::lock_guard<std::mutex> lock(queueMutex_);
        jobQueue_.push(job);
        statTotalJobs_++;
    }

    taskCv_.notify_one();
}

void ConcurrencyManager::submitTransfer(const std::string& jobId, int sender, int receiver, double amount) {
    submitTransaction({jobId, TransactionJobType::TRANSFER, sender, receiver, amount, nullptr});
}

void ConcurrencyManager::submitDeposit(const std::string& jobId, int account, double amount) {
    submitTransaction({jobId, TransactionJobType::DEPOSIT, 0, account, amount, nullptr});
}

void ConcurrencyManager::submitWithdraw(const std::string& jobId, int account, double amount) {
    submitTransaction({jobId, TransactionJobType::WITHDRAW, account, 0, amount, nullptr});
}

void ConcurrencyManager::waitForCompletion() {
    std::unique_lock<std::mutex> lock(queueMutex_);
    completionCv_.wait(lock, [this]() {
        return jobQueue_.empty() && statActiveThreads_.load() == 0;
    });
}

void ConcurrencyManager::shutdown() {
    if (!isRunning_.load()) {
        return;
    }

    stopRequested_ = true;
    taskCv_.notify_all();

    for (auto& th : workers_) {
        if (th.joinable()) {
            th.join();
        }
    }

    workers_.clear();
    isRunning_ = false;
    Logger::info("ConcurrencyManager workers successfully joined and shut down.", "ConcurrencyManager");
}

bool ConcurrencyManager::isRunning() const noexcept {
    return isRunning_.load();
}

void ConcurrencyManager::updateWorkerState(const std::string& workerId, ThreadState state, const std::string& task) {
    std::lock_guard<std::mutex> lock(registryMutex_);
    auto it = workerRegistry_.find(workerId);
    if (it != workerRegistry_.end()) {
        it->second.state = state;
        if (!task.empty()) {
            it->second.currentTask = task;
        }
    }
}

WorkerInfo ConcurrencyManager::getWorkerInfo(const std::string& workerId) const {
    std::lock_guard<std::mutex> lock(registryMutex_);
    auto it = workerRegistry_.find(workerId);
    if (it != workerRegistry_.end()) {
        return it->second;
    }
    return {workerId, ThreadState::FAILED, "Worker Not Found", 0};
}

std::vector<WorkerInfo> ConcurrencyManager::getAllWorkers() const {
    std::lock_guard<std::mutex> lock(registryMutex_);
    std::vector<WorkerInfo> list;
    list.reserve(workerRegistry_.size());
    for (const auto& kv : workerRegistry_) {
        list.push_back(kv.second);
    }
    std::sort(list.begin(), list.end(), [](const WorkerInfo& a, const WorkerInfo& b) {
        return a.workerId < b.workerId;
    });
    return list;
}

ConcurrencySystemStats ConcurrencyManager::getSystemStats() const {
    return {
        statTotalJobs_.load(),
        statCompletedJobs_.load(),
        statFailedJobs_.load(),
        statActiveThreads_.load(),
        statPeakThreads_.load()
    };
}

void ConcurrencyManager::resetStats() {
    statTotalJobs_ = 0;
    statCompletedJobs_ = 0;
    statFailedJobs_ = 0;
    statActiveThreads_ = 0;
    statPeakThreads_ = 0;
}

// ----------------------------------------------------------------------------
// High-Level Batch Helpers
// ----------------------------------------------------------------------------

ConcurrencyMetrics ConcurrencyManager::executeBatch(const std::vector<TransferRequest>& transfers) {
    return processor_.processConcurrentTransfers(transfers);
}

ConcurrencyMetrics ConcurrencyManager::executeWithdrawals(const std::vector<WithdrawalRequest>& withdrawals) {
    return processor_.processConcurrentWithdrawals(withdrawals);
}

void ConcurrencyManager::runRaceConditionDemo(double balance, double w1, double w2) {
    processor_.demonstrateRaceCondition(balance, w1, w2);
}

ConcurrentTransactionProcessor& ConcurrencyManager::getProcessor() noexcept {
    return processor_;
}

// ----------------------------------------------------------------------------
// Isolated Shared Counter Critical Section & Race Condition Demonstration
// ----------------------------------------------------------------------------

CounterDemoResult ConcurrencyManager::demonstrateCounterRaceCondition(int numThreads, int incrementsPerThread) {
    CounterDemoResult result;
    result.numThreads = numThreads;
    result.incrementsPerThread = incrementsPerThread;
    result.expectedCount = numThreads * incrementsPerThread;

    Logger::info("Starting Shared Counter Race Condition Demonstration (" + 
                 std::to_string(numThreads) + " threads x " + 
                 std::to_string(incrementsPerThread) + " increments)...", "ConcurrencyManager");

    // ========================================================================
    // Part 1: Unsynchronized Counter (Demonstrates Race Condition & Lost Updates)
    // ========================================================================
    volatile int unsafeCounter = 0;
    std::vector<std::thread> unsafeThreads;
    unsafeThreads.reserve(numThreads);

    for (int i = 0; i < numThreads; ++i) {
        unsafeThreads.emplace_back([&unsafeCounter, incrementsPerThread]() {
            for (int j = 0; j < incrementsPerThread; ++j) {
                // Non-atomic read-modify-write introduces classic data races
                int temp = unsafeCounter;
                if (j % 50 == 0) {
                    std::this_thread::yield(); // Encourage context switching
                }
                unsafeCounter = temp + 1;
            }
        });
    }

    for (auto& th : unsafeThreads) {
        if (th.joinable()) th.join();
    }

    result.unsafeFinalCount = unsafeCounter;
    result.raceConditionOccurred = (result.unsafeFinalCount != result.expectedCount);

    Logger::warn("Unsynchronized Final Count: " + std::to_string(result.unsafeFinalCount) +
                 " (Expected: " + std::to_string(result.expectedCount) + 
                 " | Lost Updates: " + std::to_string(result.expectedCount - result.unsafeFinalCount) + ")", "ConcurrencyManager");

    // ========================================================================
    // Part 2: Mutex-Protected Counter (Demonstrates Mutual Exclusion)
    // ========================================================================
    int safeCounter = 0;
    std::mutex counterMutex;
    std::vector<std::thread> safeThreads;
    safeThreads.reserve(numThreads);

    for (int i = 0; i < numThreads; ++i) {
        safeThreads.emplace_back([&safeCounter, &counterMutex, incrementsPerThread]() {
            for (int j = 0; j < incrementsPerThread; ++j) {
                std::lock_guard<std::mutex> lock(counterMutex);
                safeCounter++;
            }
        });
    }

    for (auto& th : safeThreads) {
        if (th.joinable()) th.join();
    }

    result.safeFinalCount = safeCounter;
    Logger::info("Synchronized Final Count: " + std::to_string(result.safeFinalCount) +
                 " (Deterministic & Exact match with expected " + std::to_string(result.expectedCount) + ")", "ConcurrencyManager");

    return result;
}

} // namespace paylock
