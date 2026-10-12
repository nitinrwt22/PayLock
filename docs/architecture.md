# PayLock Architecture & Design Documentation

## Overview
PayLock is a concurrent payment transaction management system designed for Project-Based Learning (PBL). It unifies core principles from Operating Systems (OS) and Database Management Systems (DBMS).

---

## Key Core Concepts

### 1. Operating Systems (OS)
- **Concurrency & Multithreading (Day 3+)**: Worker threads processing multiple transaction requests concurrently.
- **Mutual Exclusion & Locks (Day 3+)**: Thread synchronization using mutexes, reader-writer locks, and transaction-level resource locks.
- **Deadlock Handling (Day 4+)**: Detection algorithms (Wait-For Graph / cycle detection) and resolution via victim rollback.
- **Thread-Safety (Day 1)**: Internal mutex protection (`std::mutex`) within `DatabaseManager` ensuring serial execution on shared connection handles.

### 2. Database Management Systems (DBMS)
- **ACID Guarantees**:
  - **Atomicity**: Either all operations in a payment transfer succeed (debit sender + credit receiver + write log), or none do (`ROLLBACK`).
  - **Consistency**: Database constraints (e.g., `CHECK (balance >= 0)`, `CHECK (amount > 0)`) ensure accounts never enter illegal states.
  - **Isolation**: Transaction isolation levels preventing dirty reads, non-repeatable reads, and phantom reads.
  - **Durability**: Committed transactions are persisted to disk via MySQL InnoDB's write-ahead logging (WAL) and doublewrite buffering.
- **Transaction Logs**: Audit logs (`transaction_logs` table) recording lifecycle operations (`START`, `DEBIT`, `CREDIT`, `COMMIT`, `ROLLBACK`).

---

## Day 1 Architecture & Modular Layout

```
PayLock/
├── CMakeLists.txt              # Cross-platform build configuration
├── Makefile                    # Fast direct compilation
├── README.md                   # Project overview, setup, and instructions
├── docs/                       # Architecture and project documentation
│   └── architecture.md
├── sql/                        # SQL DDL & Seed scripts
│   ├── schema.sql              # Table definitions and constraints
│   └── seed.sql                # Initial test users & accounts
├── src/                        # Core C++ implementation
│   ├── main.cpp                # Day 3 verification entry point
│   ├── concurrency/            # Day 3 Concurrency & OS Layer
│   │   ├── ConcurrentTransactionProcessor.h
│   │   └── ConcurrentTransactionProcessor.cpp
│   ├── database/
│   │   ├── DatabaseManager.h   # MySQL C client wrapper & RAII lifecycle
│   │   └── DatabaseManager.cpp
│   ├── models/                 # Domain data structures
│   │   ├── Account.h
│   │   ├── Transaction.h
│   │   ├── TransactionLog.h
│   │   └── User.h
│   └── transaction/
│       ├── TransactionManager.h   # Business layer transaction orchestrator
│       └── TransactionManager.cpp
└── tests/
    └── test_db.cpp             # Automated integration & concurrency tests
```

---

## Day 3 Completed: Concurrency, Multithreading & Synchronization
The OS concurrency layer is fully operational and integrated with MySQL:
1. `ConcurrentTransactionProcessor` coordinating worker threads via `std::thread`.
2. Critical section protection via `std::mutex` and `std::lock_guard`.
3. In-memory Race Condition demonstration contrasting unsynchronized TOCTOU races with mutex-protected mutual exclusion.
4. Per-thread isolated database connections preventing socket contention.
5. Real concurrent transfer processing across accounts with programmatic balance verification against MySQL.
6. Same-account concurrent withdrawal contention and overdraft rejection.
7. Thread-safe atomic console logging via dedicated `consoleMutex_`.
8. Execution performance measurement using `std::chrono`.

---

## Forward Compatibility for Day 4 Sprint
Day 4 will introduce Deadlock Handling & Advanced Lock Management:
1. Deadlock detection via Wait-For Graph (WFG) cycle detection.
2. Deadlock resolution via victim transaction abort & rollback.
3. Fine-grained row-level / account-level locking protocols.
