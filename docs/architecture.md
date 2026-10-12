# PayLock Architecture & Design Documentation

## System Overview
**PayLock** is an enterprise-grade concurrent payment transaction management system designed for Project-Based Learning (PBL). It bridges principles from **Operating Systems (OS)** and **Database Management Systems (DBMS)** to guarantee consistency, isolation, and durability under high concurrency.

---

## Conceptual Architecture Flow

```text
               Frontend / Dashboard [IN PROGRESS]
                         │
                         ▼
                  API Layer [PLANNED]
                         │
                         ▼
             Transaction Manager [BUILT]
                         │
                         ▼
             Concurrency Manager [BUILT]
                         │
                         ▼
                Lock Manager [IN PROGRESS]
                         │
                         ▼
              Deadlock Detector [IN PROGRESS]
                         │
                         ▼
              Recovery Manager [IN PROGRESS]
                         │
                         ▼
              Database / MySQL [BUILT]

                  Resource Manager [IN PROGRESS]
                         ↕
         Concurrency / Transaction System

                 Centralized Logger [BUILT]
           (Receives events across all layers)
```

---

## Module Breakdown & Implementation Status

| Module | Category | Primary Responsibility | Status |
|---|---|---|---|
| **DatabaseManager** | DBMS | MySQL connection pooling, queries, prepared statements, and transactional primitives. | `[BUILT]` |
| **Models (`User`, `Account`, `Transaction`, `TransactionLog`)** | Domain | Type-safe financial data representations with strict non-floating decimal fields. | `[BUILT]` |
| **TransactionManager** | DBMS / Core | Financial transfer, deposit, withdrawal workflows, and ACID lifecycle coordination (`START TRANSACTION`, `COMMIT`, `ROLLBACK`). | `[BUILT]` |
| **ConcurrencyManager** | OS | Multithreading with `std::thread`, thread lifecycle state transitions, and thread-safe batch processing. | `[BUILT]` |
| **ConcurrentTransactionProcessor** | OS | Critical section protection via `std::mutex` / `std::lock_guard`, preventing TOCTOU race conditions. | `[BUILT]` |
| **Logger** | Cross-cutting | Thread-safe, synchronized logging supporting `DEBUG`, `INFO`, `WARNING`, `ERROR` levels with component tags. | `[BUILT]` |
| **LockManager** | OS / DBMS | Shared (S) and Exclusive (X) row-level account locking adhering to Strict 2-Phase Locking (2PL). | `[IN PROGRESS]` |
| **DeadlockDetector** | OS / DBMS | Wait-For Graph (WFG) maintenance and directed cycle detection algorithms (Tarjan's / DFS). | `[IN PROGRESS]` |
| **RecoveryManager** | OS / DBMS | Deadlock resolution via victim selection policies and transaction rollback. | `[IN PROGRESS]` |
| **ResourceManager** | OS | Memory allocation, resource tracking, and OS paging simulation abstractions. | `[IN PROGRESS]` |
| **API Layer** | Distributed | REST / HTTP endpoints connecting client applications to TransactionManager. | `[PLANNED]` |
| **Frontend Dashboard** | Presentation | Real-time transaction visualization, thread state monitors, and lock inspection dashboard. | `[IN PROGRESS]` |

---

## Operating Systems (OS) Concepts Mapping

1. **Multithreading**: Independent worker threads (`std::thread`) running concurrently across CPU cores.
2. **Mutual Exclusion**: Guarding critical sections with `std::mutex` and `std::lock_guard`.
3. **Race Conditions**: Preventing Time-of-Check to Time-of-Use (TOCTOU) inconsistencies on shared accounts.
4. **Thread States**: Tracking `CREATED` -> `WAITING_FOR_MUTEX` -> `RUNNING` -> `COMPLETED` / `FAILED`.
5. **Deadlock Management**: Detection via Wait-For Graph (WFG) cycle analysis and victim rollback.
6. **Resource Management**: Safe vs. unsafe states, resource graphs, and memory page simulation.

---

## Database Management Systems (DBMS) Concepts Mapping

1. **ACID Transactions**:
   - **Atomicity**: Transfers execute completely or abort with zero side-effects via MySQL `ROLLBACK`.
   - **Consistency**: Enforced through `DECIMAL(15, 2)` monetary types, `CHECK (balance >= 0.00)`, and Foreign Key constraints.
   - **Isolation**: Per-thread connection sessions preventing socket corruption; future row-level locks enforcing serializability.
   - **Durability**: Guaranteed by MySQL InnoDB's Write-Ahead Log (WAL) and redo log flushing on `COMMIT`.
2. **Audit Logging**: Write-Ahead Logging in `transaction_logs` capturing `TRANSFER_STARTED`, `SENDER_BALANCE_DEBITED`, `RECEIVER_BALANCE_CREDITED`, `TRANSACTION_COMMITTED`, `TRANSACTION_ROLLED_BACK`.
3. **Stored Procedures**: Encapsulated database-level transaction routines in `database/procedures.sql`.

---

## Directory Organization

```text
PayLock/
├── backend/
│   ├── include/                # Public modular C++ headers
│   │   ├── database/
│   │   ├── models/
│   │   ├── transaction/
│   │   ├── concurrency/
│   │   ├── locking/
│   │   ├── deadlock/
│   │   ├── recovery/
│   │   ├── resources/
│   │   └── logging/
│   └── src/                    # Implementations & main executable
│       ├── main.cpp
│       ├── database/
│       ├── models/
│       ├── transaction/
│       ├── concurrency/
│       ├── locking/
│       ├── deadlock/
│       ├── recovery/
│       ├── resources/
│       └── logging/
├── database/                   # Relational DDL, seed data & stored procedures
├── tests/                      # Automated unit, integration & concurrency suite
├── frontend/                   # UI dashboard prototype
├── docs/                       # Architectural and technical documentation
├── CMakeLists.txt              # Primary cross-platform build
├── Makefile                    # Developer quick-build tool
└── README.md                   # Project overview & roadmap
```
