# PayLock Concurrency Engine Specification

## 1. What Concurrency Means in PayLock
In PayLock, **concurrency** is the ability of the system to manage, schedule, and overlap the execution of multiple independent payment transactions simultaneously across multiple CPU cores. Instead of executing financial transactions sequentially one-by-one, PayLock leverages kernel-level worker threads (`std::thread`) to process deposits, withdrawals, and peer-to-peer transfers in parallel, significantly improving throughput and reducing transaction latency.

---

## 2. Why Payment Processing Requires Concurrency Control
In high-throughput financial environments (e.g., UPI, credit networks, banking core ledgers), thousands of requests arrive every millisecond. If multiple threads concurrently inspect and modify shared account balances without strict concurrency control, severe race conditions arise:
- **Time-of-Check to Time-of-Use (TOCTOU)**: Two concurrent requests check the same account balance simultaneously, both pass the solvency test, and both execute debits, resulting in disastrous negative overdrafts.
- **Lost Updates**: One worker's balance write overwrites an uncommitted balance modification from another worker running simultaneously.
- **Data Inconsistency**: Partially executed transfers corrupt account totals and violate the fundamental accounting invariant that money must neither be created nor destroyed.

---

## 3. Multithreading Model (`std::thread`)
PayLock uses standard C++17 `std::thread` mapping 1:1 to OS kernel threads (POSIX `pthread` on Darwin/macOS and Linux):
- **Real Concurrency**: All worker threads are launched in batch or maintained in an active worker pool waiting on a task queue before joining. Execution is never artificially serialized by creating and immediately joining threads.
- **Worker Thread Pool Architecture**:
  ```text
                  Concurrent Requests
                          │
                          ▼
                ┌────────────────────┐
                │ ConcurrencyManager │
                └─────────┬──────────┘
                          │ Job Queue (std::queue<TransactionJob>)
                          │ Guarded by std::mutex & std::condition_variable
                          ▼
            ┌─────────────┼─────────────┐
            ▼             ▼             ▼
         Worker 1      Worker 2      Worker 3   ... Worker N
         (std::thread) (std::thread) (std::thread)
            │             │             │
            ▼             ▼             ▼
       [Dedicated DB] [Dedicated DB] [Dedicated DB]
  ```
- **Thread Lifecycle Management**: Workers are joined safely upon `shutdown()` or pool destruction, preventing dangling threads or `std::terminate()`.

---

## 4. Mutual Exclusion (`std::mutex`)
A **mutex** (mutual exclusion lock) is an operating system synchronization primitive that guarantees that at most one thread can execute within a protected code region at any given moment:
- `std::mutex`: Fundamental OS synchronization mechanism protecting shared mutable state.
- `std::lock_guard<std::mutex>`: RAII-based scoped lock that automatically acquires the mutex on construction and guarantees release upon scope exit (even when exceptions occur).
- `std::unique_lock<std::mutex>`: Flexible lock used in combination with condition variables for non-busy waiting on job queues.

---

## 5. Critical Section
The **critical section** is the specific block of code that accesses and mutates shared memory or shared mutable resources:
```cpp
{
    // ENTER CRITICAL SECTION
    std::lock_guard<std::mutex> lock(criticalSectionMutex_);

    // Check-Then-Act Sequence (Atomic in Application Memory)
    if (senderAccount.balance >= transferAmount) {
        workerTx.transfer(senderId, receiverId, amount);
    }
    // EXIT CRITICAL SECTION (Mutex Automatically Released)
}
```
Inside this critical section, concurrent worker threads are serialized, ensuring that no thread is preempted between reading an account balance and recording the debit.

---

## 6. Race Conditions & Counter Demonstration
A **race condition** occurs when multiple threads concurrently access shared mutable data and the outcome depends on the non-deterministic interleaving of thread execution by the OS scheduler.

### Shared Counter Demonstration (`demonstrateCounterRaceCondition`)
To verify race conditions safely without corrupting banking ledger data, PayLock implements an isolated shared counter benchmark:
- **Unsynchronized Execution**: 10 threads each increment a shared counter 10,000 times without synchronization. Non-atomic read-modify-write instructions (`temp = counter; counter = temp + 1`) suffer heavy lost updates, resulting in final counts far below the expected 100,000 (typically ~10,000–30,000).
- **Synchronized Execution**: 10 threads perform the same loop protected by `std::mutex`. Mutual exclusion ensures serial execution of the increment, guaranteeing an exact, deterministic count of **100,000**.

---

## 7. Thread States & Lifecycle
Worker threads transition through explicit, thread-safe lifecycle states:
```text
[CREATED] ────────► [WAITING] ────────► [RUNNING]
                       ▲                    │
                       │                    ├───────────────► [COMPLETED]
                       │ (Job Done)         │
                       └────────────────────┴───────────────► [FAILED]
```
- **`CREATED`**: Thread spawned, assigned worker ID (e.g. `W001`), and initialized.
- **`WAITING`**: Worker waiting for jobs in the queue via `std::condition_variable::wait()`.
- **`WAITING_FOR_MUTEX`**: Worker attempting to acquire the critical section lock.
- **`RUNNING`**: Worker actively executing a database transaction.
- **`COMPLETED`**: Transaction committed and permanently written.
- **`FAILED`**: Transaction aborted or rolled back due to insufficient balance or validation errors.

---

## 8. Thread-Safe Logging
When multiple threads log concurrently to `std::cout`, character streams can become interleaved and illegible (e.g. `[T1] Trans[T2] actio...`).
- PayLock solves this via a centralized, static mutex (`consoleMutex_`) inside [`backend/include/logging/Logger.h`](file:///Users/nitinrawat/PayLock/backend/include/logging/Logger.h).
- Every log message is formatted completely in memory before acquiring `consoleMutex_`, guaranteeing clean, atomic line-by-line output.

---

## 9. Concurrent Transaction Execution Workflow
1. Client submits transactions (`submitTransfer`, `submitDeposit`, `submitWithdraw`) into `ConcurrencyManager`.
2. Job is enqueued into `jobQueue_` and worker threads waiting on `std::condition_variable` are notified (`taskCv_.notify_one()`).
3. An available worker thread awakes, transitions from `WAITING` to `RUNNING`, and extracts the job.
4. The worker uses its **own isolated MySQL connection** to invoke `TransactionManager`.
5. The critical section serializes balance checks and debits, triggering `START TRANSACTION;` $\rightarrow$ `COMMIT;` in MySQL.
6. Statistics (`completedTransactions`, `activeThreads`, `peakConcurrentThreads`) are updated atomically (`std::atomic`).
7. Worker transitions to `COMPLETED` and awaits the next task.

---

## 10. Application Synchronization vs. Database Locking

| Property | Application Mutex (`std::mutex`) | Database Row Locking (InnoDB) |
|---|---|---|
| **Layer** | Operating System / C++ Application Memory | Relational Database Engine (MySQL InnoDB) |
| **Scope** | Guards in-memory variables, worker queues, and critical sections | Guards database records, table pages, and disk indexes |
| **Mechanism** | OS kernel futex / pthread mutex | Row-level locks, gap locks, undo/redo logs |
| **Purpose** | Prevents CPU-level TOCTOU races in application memory | Guarantees ACID serializability across independent DB clients |

---

## 11. Current Limitations
- **Coarse-Grained Critical Section**: Balance modifications across all accounts currently share `criticalSectionMutex_`, preventing independent accounts (e.g. Account A $\rightarrow$ B and Account C $\rightarrow$ D) from updating concurrently.
- **No In-Memory 2-Phase Locking**: Locks are currently acquired at transaction entry and released immediately on completion rather than using a formal growing/shrinking lock phase.

---

## 12. How Future Lock Manager (Step 4) Will Extend This
In **Step 4 (Lock Manager)**:
- Coarse-grained locking will be replaced by **fine-grained account-level row locking** (`LockManager`).
- Support for **Shared (S) locks** (read-only balance checks) and **Exclusive (X) locks** (debit/credit balance mutations) adhering to **Strict 2-Phase Locking (Strict 2PL)**.
- Multiple transactions touching non-overlapping accounts will execute in true parallel fashion without mutual contention.
