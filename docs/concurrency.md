# PayLock Concurrency & Multithreading Specification

## Overview
PayLock demonstrates how modern operating systems handle concurrent execution, thread scheduling, synchronization, and race condition prevention when interfacing with transactional databases.

---

## Multithreading Architecture

1. **Thread Model**: 1:1 Kernel-level threading via `std::thread` (backed by native POSIX `pthread` on macOS / Linux).
2. **Scheduling**: Preemptive Multi-Level Feedback Queue (MLFQ) on Darwin / Mach kernel (and Completely Fair Scheduler / CFS on Linux).
3. **No Fake Concurrency**: All worker threads are launched before any thread is joined:
   ```cpp
   std::vector<std::thread> workers;
   for (const auto& req : requests) {
       workers.emplace_back([&]() { process(req); });
   }
   for (auto& th : workers) {
       th.join();
   }
   ```

---

## Critical Section & Mutex Protection

### The Problem: Time-of-Check to Time-of-Use (TOCTOU)
When two threads concurrently read the balance of Account A (₹10,000) and both attempt to withdraw ₹7,000 and ₹6,000 respectively:
- Without a mutex:
  - Thread 1 checks: `10000 >= 7000` (Approved). Thread 1 is preempted.
  - Thread 2 checks: `10000 >= 6000` (Approved).
  - Both debit the account, yielding an illegal negative balance: **₹-3,000**.
- With `std::mutex` and `std::lock_guard`:
  - Thread 1 acquires the mutex, enters the critical section, debits ₹7,000, updates balance to ₹3,000, and releases the mutex.
  - Thread 2 enters, reads balance = ₹3,000, fails check (`3000 < 6000`), and is safely rejected.
  - Final balance is preserved at **₹3,000**.

---

## Thread Lifecycle States

```text
[CREATED] ────────► [WAITING_FOR_MUTEX] ────────► [RUNNING]
                                                     │
                             ┌───────────────────────┴───────────────────────┐
                             ▼                                               ▼
                        [COMPLETED]                                       [FAILED]
```
