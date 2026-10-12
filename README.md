# PayLock — A Concurrent Payment Transaction Management System

**PayLock** is an enterprise-grade payment transaction processing engine designed to demonstrate the deep intersection of **Operating Systems (OS)** concurrency principles and **Database Management Systems (DBMS)** transactional consistency guarantees within a unified, high-performance C++ and MySQL architecture.

---

## Problem
High-throughput payment networks (such as UPI, SWIFT, and core banking gateways) encounter acute architectural challenges when processing thousands of concurrent fund transfers:
1. **Concurrency Issues (OS Level)**: When multiple operating system worker threads inspect and manipulate shared account memory or database records simultaneously, non-atomic "Check-Then-Act" sequences cause Time-of-Check to Time-of-Use (TOCTOU) race conditions, data corruption, and catastrophic negative balance overdrafts.
2. **Transaction Consistency Issues (DBMS Level)**: Partial transfer failures (e.g., deducting from sender but crashing before crediting receiver) violate consistency. Without strict ACID enforcement, dirty reads, non-repeatable reads, lost updates, and phantom data corrupt relational integrity.
3. **Deadlock Hazards**: Concurrent cross-transfers (e.g., Thread 1 transferring Account A → Account B while Thread 2 transfers Account B → Account A) introduce circular resource-wait conditions, locking system execution indefinitely.

---

## Objective
PayLock combines foundational **Operating Systems concepts** and **DBMS transaction guarantees** into a cohesive system:
- **OS Concepts**: Multithreading (`std::thread`), mutex synchronization (`std::mutex`, `std::lock_guard`), critical section protection, thread state lifecycle tracking, Wait-For Graph (WFG) cycle detection, dynamic memory page allocation/translation simulation, and thread-safe logging.
- **DBMS Concepts**: Strict ACID guarantees (Atomicity, Consistency, Isolation, Durability) backed by MySQL InnoDB, Write-Ahead Audit Logging (WAL), relational foreign key constraints, `CHECK` domains, and row-level 2-Phase Locking (2PL).

---

## Current Architecture

PayLock is organized into a modular layered architecture:

```text
               ┌────────────────────────────────────────────────────────┐
               │         Frontend Dashboard / Visualization             │
               │            (HTML5 / CSS3 / JavaScript)                 │
               └──────────────────────────┬─────────────────────────────┘
                                          │ [PLANNED HTTP/REST]
                                          ▼
               ┌────────────────────────────────────────────────────────┐
               │                    REST API Layer                      │
               │        (HTTP Endpoints & Request Serialization)        │
               └──────────────────────────┬─────────────────────────────┘
                                          │
                                          ▼
               ┌────────────────────────────────────────────────────────┐
               │                  TransactionManager                    │
               │   - Transfer, Deposit, Withdraw business orchestration │
               │   - ACID lifecycle: BEGIN, COMMIT, ROLLBACK            │
               └──────────┬─────────────────────────────────┬───────────┘
                          │                                 │
                          ▼                                 ▼
         ┌─────────────────────────────────┐   ┌────────────────────────┐
         │       ConcurrencyManager        │   │    ResourceManager     │
         │ - Worker threads (std::thread)  │   │ - Memory page alloc    │
         │ - Critical sections & Mutex     │   │ - Virtual address tr.  │
         └────────────────┬────────────────┘   └────────────────────────┘
                          │
                          ▼
         ┌─────────────────────────────────┐
         │           LockManager           │
         │ - Shared / Exclusive row locks  │
         │ - 2-Phase Locking (2PL) protocol│
         │ - Lock table & wait queues      │
         └────────────────┬────────────────┘
                          │
                          ▼
         ┌─────────────────────────────────┐
         │        DeadlockDetector         │
         │ - Wait-For Graph (WFG) analysis │
         │ - Cycle detection via DFS       │
         └────────────────┬────────────────┘
                          │
                          ▼
         ┌─────────────────────────────────┐
         │         RecoveryManager         │
         │ - Victim selection policies     │
         │ - Transaction abort & rollback  │
         └────────────────┬────────────────┘
                          │
                          ▼
         ┌────────────────────────────────────────────────────────┐
         │                  DatabaseManager                       │
         │ - MySQL connection lifecycle (libmysqlclient)          │
         │ - Prepared statements & SQL execution                  │
         │ - WAL audit logging (transaction_logs)                 │
         └────────────────────────────────────────────────────────┘

    [Centralized Thread-Safe Logger receives telemetry across all subsystems]
```

### Modular Directory Structure
```text
PayLock/
├── backend/
│   ├── include/
│   │   ├── database/       # DatabaseManager.h, DatabaseConfig.h
│   │   ├── models/         # User.h, Account.h, Transaction.h, TransactionLog.h
│   │   ├── transaction/    # TransactionManager.h
│   │   ├── concurrency/    # ConcurrencyManager.h, ConcurrentTransactionProcessor.h
│   │   ├── locking/        # LockManager.h (Shared / Exclusive locks)
│   │   ├── deadlock/       # DeadlockDetector.h (WFG & Cycle detection)
│   │   ├── recovery/       # RecoveryManager.h (Victim selection & abort)
│   │   ├── resources/      # ResourceManager.h (Page allocation & translation)
│   │   └── logging/        # Logger.h (Thread-safe multi-level telemetry)
│   │
│   └── src/
│       ├── main.cpp        # System orchestrator & evaluation CLI
│       ├── database/       # DatabaseManager.cpp
│       ├── models/         # Model implementations
│       ├── transaction/    # TransactionManager.cpp
│       ├── concurrency/    # ConcurrencyManager.cpp, ConcurrentTransactionProcessor.cpp
│       ├── locking/        # LockManager.cpp
│       ├── deadlock/       # DeadlockDetector.cpp
│       ├── recovery/       # RecoveryManager.cpp
│       ├── resources/      # ResourceManager.cpp
│       └── logging/        # Logger.cpp
│
├── database/
│   ├── schema.sql          # Relational tables, CHECK constraints, FK cascades
│   ├── seed.sql            # Seed accounts (Nitin: ₹10,000, Ansh: ₹5,000, Megha: ₹7,000)
│   └── procedures.sql      # Stored procedures (sp_transfer_funds)
│
├── tests/
│   └── test_db.cpp         # Comprehensive 22-test automated test suite
│
├── frontend/
│   ├── index.html          # Interactive dark-mode dashboard
│   ├── css/style.css       # Glassmorphism visual styles
│   ├── js/app.js           # Client-side dynamic simulation & future API client
│   └── README.md           # Frontend documentation
│
├── docs/
│   ├── architecture.md     # Architectural blueprint & subsystem mapping
│   ├── database.md         # Relational schema, normalization & ACID specification
│   ├── concurrency.md      # Multithreading, synchronization & critical sections
│   ├── deadlock.md         # WFG theory, detection algorithms & recovery strategies
│   └── api.md              # REST API endpoint contracts
│
├── CMakeLists.txt          # CMake C++17 build specification
├── Makefile                # Fast CLI compilation & test runner
├── README.md               # Project documentation
└── .gitignore              # Build and IDE artifact exclusions
```

---

## Technology Stack
- **Language**: C++17 (Compiled with Apple Clang / GCC)
- **Database**: MySQL 9.x (InnoDB Engine, ACID compliance, WAL redo logs)
- **Database Driver**: MySQL C/C++ Client Library (`libmysqlclient` / `<mysql.h>`)
- **Build System**: CMake (v3.15+) and Make
- **Frontend Prototype**: HTML5, CSS3 (Modern Glassmorphic Dark UI), Vanilla JavaScript ES6

---

## Current Status

| Subsystem / Module | Responsibility | Status |
|---|---|---|
| **DatabaseManager** | MySQL connection pool, prepared queries, transaction commands | **[BUILT]** |
| **Domain Models** | User, Account, Transaction, TransactionLog value objects | **[BUILT]** |
| **TransactionManager** | Transfer, deposit, withdraw, balance validation, ACID rollback | **[BUILT]** |
| **Concurrent Processor** | `std::thread` concurrent workers, race-condition prevention, mutex guards | **[BUILT]** |
| **Logger** | Centralized thread-safe console & structured event logging | **[BUILT]** |
| **ConcurrencyManager** | Facade for thread execution, worker scheduling & contention control | **[BUILT]** |
| **LockManager** | Shared (S) and Exclusive (X) 2-Phase Locking interface and stub | **[IN PROGRESS / STUBBED]** |
| **DeadlockDetector** | Wait-For Graph (WFG) cycle detection interface and DFS stub | **[IN PROGRESS / STUBBED]** |
| **RecoveryManager** | Deadlock victim selection policies and transaction rollback stub | **[IN PROGRESS / STUBBED]** |
| **ResourceManager** | Virtual address to page translation and resource allocation stub | **[IN PROGRESS / STUBBED]** |
| **Frontend Prototype** | Interactive web dashboard visualizing balances, threads, and logs | **[BUILT (Prototype)]** |
| **REST API Layer** | Embedded HTTP server exposing `/api/transactions` and `/api/locks` | **[PLANNED]** |

---

## Transaction Engine

PayLock's core payment engine (`TransactionManager`) manages financial operations with strict ACID compliance:

### 1. Operations Supported
- **Peer-to-Peer Transfer (`transfer(senderId, receiverId, amount)`)**:
  - Validates amount (> 0), account existence, and distinct accounts.
  - Verifies sender balance using integer-cent arithmetic (`toCents()`).
  - Initiates atomic MySQL transaction (`START TRANSACTION;`).
  - Debits sender, credits receiver, writes audit logs to `transaction_logs`.
  - Commits transaction (`COMMIT;`) and updates status to `COMMITTED`.
  - On any failure: Immediately executes `ROLLBACK;`, leaving balances completely intact.
- **Deposit (`deposit(accountId, amount)`)**:
  - Validates account existence and positive amount.
  - Credits account inside an atomic transaction; logs external source (`sender_account = NULL`).
  - Commits transaction permanently to MySQL.
- **Withdrawal (`withdraw(accountId, amount)`)**:
  - Validates account existence, amount, and verifies `balance >= amount`.
  - If insufficient funds: Rejects without modifying ledger, marks transaction as `FAILED`, and logs warning.
  - If balance is sufficient: Debits account and commits transaction.

### 2. Transaction Lifecycle & Status Transitions
- `PENDING` -> `ACTIVE` (in-flight) -> `COMMITTED` (successful)
- `PENDING` -> `ACTIVE` -> `ROLLED_BACK` (aborted after start)
- `PENDING` -> `FAILED` (rejected before balance modification)

### 3. ACID Behavior Preserved
- **Atomicity**: Either debit and credit both succeed, or `ROLLBACK` reverts partial changes. Verified via `demonstrateControlledRollback()`.
- **Consistency**: Enforced via relational foreign keys (`fk_tx_sender`, `fk_tx_receiver`) and MySQL check constraints (`chk_account_balance`, `chk_tx_amount`).
- **Isolation**: Row-level transaction scoping within MySQL InnoDB prevents uncommitted state leakage.
- **Durability**: Committed data is durably flushed to disk via MySQL InnoDB's Write-Ahead Log (WAL) redo logs.

### 4. Transaction History API
- `getTransaction(int transactionId)`: Retrieve a single transaction record by ID.
- `getTransactions()` / `getAllTransactions()`: Retrieve complete chronological ledger history.
- `getTransactionsForAccount(int accountId)`: Retrieve filtered history for a specific account.
- `getTransactionLogs(int transactionId)`: Retrieve granular WAL audit trail for a transaction.

### 5. Controlled Rollback Demonstration
The engine includes a dedicated proof method (`demonstrateControlledRollback(senderId, receiverId, amount)`) for college evaluation:
1. Records initial balances of Account 101 and Account 102.
2. Starts MySQL transaction and creates an `ACTIVE` transaction record.
3. Debits Account 101.
4. Intentionally triggers a simulated catastrophic failure before crediting Account 102.
5. Catches exception and executes MySQL `ROLLBACK;`.
6. Re-queries MySQL balances, proving that Account 101 was restored to its exact original balance and Account 102 was never touched.

---

## Database Setup

### 1. Execute SQL Scripts
Initialize the database schema, seed accounts, and stored procedures in MySQL:
```bash
mysql -u root -p < database/schema.sql
mysql -u root -p < database/seed.sql
mysql -u root -p < database/procedures.sql
```

### 2. Configure Environment Variables (Optional)
Sensible defaults (`host=127.0.0.1`, `user=root`, `port=3306`, `database=paylock_db`) are included. Override via environment variables if needed:
```bash
export PAYLOCK_DB_HOST="127.0.0.1"
export PAYLOCK_DB_USER="root"
export PAYLOCK_DB_PASS="your_password"
export PAYLOCK_DB_NAME="paylock_db"
export PAYLOCK_DB_PORT="3306"
```

---

## Build Instructions

### Option A: Using Make (Fastest)
```bash
# Compile both the main application and test suite
make

# Clean build artifacts
make clean
```

### Option B: Using CMake
```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

---

## Run Instructions

### 1. Run the Main Demonstration
```bash
./paylock
# Or via make:
make run
```
The demonstration runs:
- Day 1: Foundational database connectivity & schema verification
- Day 2: ACID transactions, peer-to-peer transfers, deposits, withdrawals, and rollback handling
- Day 3: Multithreaded concurrent execution, mutex synchronization, and in-memory race condition benchmarks
- Subsystem Telemetry: Architectural subsystem verification (Logger, LockManager, DeadlockDetector, RecoveryManager, ResourceManager)

### 2. Run the Automated Test Suite
```bash
./test_db
# Or via make:
make test
```
Executes all 19 unit, integration, concurrency, and architecture tests.

### 3. Open the Frontend Dashboard
```bash
# Option 1: Direct browser launch
open frontend/index.html

# Option 2: Local HTTP server
python3 -m http.server 8080 --directory frontend
```

---

## Future Modules
In upcoming phases, the established interfaces will be populated with full implementations:
- **Lock Manager**: Granular row-level 2-Phase Locking (2PL), shared read locks, exclusive write locks, lock wait queues.
- **Deadlock Detection**: Periodic Wait-For Graph (WFG) cycle detection using Tarjan's or Kosaraju's DFS cycle discovery.
- **Deadlock Recovery**: Automated victim selection (Youngest Transaction, Lowest Cost, Fewest Locks), transaction abort, lock release, and automatic client retry.
- **Resource Management**: Operating systems paging simulation, memory frame allocation algorithms (FIFO, LRU), and resource request/allocation matrices.
- **REST API Layer**: Embedded C++ HTTP server bridging the C++ backend to the web dashboard via standard JSON endpoints (`GET /api/accounts`, `POST /api/transfer`, `GET /api/deadlocks`).
- **Frontend Integration**: Full live bi-directional dashboard synchronization with real-time WebSocket or Server-Sent Events (SSE) telemetry.
