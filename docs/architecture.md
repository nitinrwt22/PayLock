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
│   ├── main.cpp                # Day 1 verification entry point
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
    └── test_db.cpp             # Automated integration tests
```

---

## Day 2 Completed: Core ACID Transaction Engine
The core business transaction orchestrator has been completed:
1. `TransactionManager::transfer(senderId, receiverId, amount)` with complete ACID semantics.
2. `TransactionManager::deposit(accountId, amount)` & `withdraw(accountId, amount)`.
3. `DatabaseManager::beginTransaction()`, `commit()`, `rollback()`.
4. Automated balance validation, exact `DECIMAL(15, 2)` arithmetic, and DBMS constraint enforcement.
5. Write-Ahead Logging in `transaction_logs` capturing lifecycle events: `TRANSFER_STARTED`, `SENDER_BALANCE_DEBITED`, `RECEIVER_BALANCE_CREDITED`, `TRANSACTION_COMMITTED`, `TRANSACTION_ROLLED_BACK`.
6. Controlled failure handling demonstrating MySQL transaction rollback and data atomicity.

---

## Forward Compatibility for Day 3 Sprint
Day 3 will introduce OS concurrency and multithreading:
1. Concurrent payment requests using `std::thread`.
2. Shared account access with `std::mutex` and critical section protection.
3. Race condition demonstration (unsynchronized vs. synchronized).
4. OS thread scheduling integrated with MySQL transactional concurrency.
