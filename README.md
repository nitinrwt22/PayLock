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
│   └── test_db.cpp         # Comprehensive 25-test automated test suite
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
| **LockManager** | Shared (S) and Exclusive (X) 2-Phase Locking, FIFO wait queues, lock upgrades | **[BUILT]** |
| **DeadlockDetector** | Wait-For Graph (WFG) snapshot synthesis, directed cycle detection (3-Color DFS), DeadlockReport generation | **[BUILT]** |
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

## Concurrency Engine

PayLock's **Concurrency Engine** (`ConcurrencyManager` & `ConcurrentTransactionProcessor`) orchestrates multithreaded transaction processing by integrating OS concurrency mechanisms with transactional database guarantees:

### 1. Multithreading & Worker Pool
- **`std::thread` Worker Pool**: Configurable pool of worker threads (`W001`, `W002`, `W003`, `W004`) continuously serving an asynchronous transaction job queue.
- **Efficient Non-Busy Waiting**: Workers block efficiently on `std::condition_variable` rather than spinning in busy-wait loops.
- **Connection Isolation**: Each worker thread maintains its **own independent MySQL connection handle**, preventing socket interleaving and cross-thread connection contention.

### 2. Synchronization & Critical Sections
- **Mutual Exclusion (`std::mutex`)**: Critical sections protecting in-memory balance verification and mutation are guarded via `std::lock_guard<std::mutex>` (RAII).
- **Time-of-Check to Time-of-Use (TOCTOU) Prevention**: Eliminates race conditions where competing threads verify solvency simultaneously and execute double debits.

### 3. Thread State Lifecycle Tracking
Every worker thread's state is tracked in a thread-safe registry:
- `CREATED` -> `WAITING` (waiting for job) -> `RUNNING` (executing transaction) -> `COMPLETED` / `FAILED`.

### 4. Controlled Demonstrations for Viva / Evaluation
- **Shared Counter Race Condition (`demonstrateCounterRaceCondition`)**:
  - Unsynchronized: $N$ threads increment counter without locks $\rightarrow$ heavy lost updates.
  - Synchronized: $N$ threads increment counter with `std::mutex` $\rightarrow$ exact deterministic count ($N \times M$).
- **Multi-Worker Payment Benchmark**:
  - Dispatches simultaneous transactions (e.g. Account A ₹20,000 transferring ₹2,000 to B, ₹3,000 to C, ₹1,000 to B).
  - Verifies that final balances in MySQL are mathematically exact (A = ₹14,000, B = ₹8,000, C = ₹8,000).

---

## Lock Manager

PayLock's **Lock Manager** (`paylock::LockManager`) provides granular application-level concurrency control over individual payment accounts, replacing coarse-grained locks with strict 2-Phase Locking (2PL):

### 1. Lock Modes & Compatibility
- **Shared (S) Lock**: Acquired for read operations (`checkBalance()`). Multiple transactions can hold Shared locks concurrently on the same account.
- **Exclusive (X) Lock**: Acquired for write operations (`transfer()`, `deposit()`, `withdraw()`). Conflicting with all other Shared and Exclusive requests.

| Requested Mode | Granted: None | Granted: Shared (S) | Granted: Exclusive (X) |
|---|---|---|---|
| **Shared (S)** | Compatible | Compatible | Incompatible |
| **Exclusive (X)** | Compatible | Incompatible | Incompatible |

### 2. Waiting Queue & Thread Wakeup
- Conflicting transactions wait in a non-busy **FIFO wait queue** backed by `std::condition_variable`.
- Releasing a lock automatically notifies queued transactions via `lockCv_.notify_all()`, re-evaluating compatibility in FIFO order without starvation.

### 3. Lock Upgrades (S → X)
- Supports safe lock upgrading: If a transaction holding a Shared lock requests an Exclusive lock, the upgrade succeeds immediately if it is the sole holder. If other shared holders exist, it waits safely on `lockCv_` until other shared holders release.

### 4. Deterministic Multi-Resource Lock Ordering
- Transfers involving multiple accounts acquire locks in a deterministic order: `min(account1, account2)` followed by `max(account1, account2)`.
- Eliminates circular AB-BA wait conditions during concurrent two-party transfers.

### 5. Transaction Lifecycle Integration
- Integrated directly with `TransactionManager` via RAII scoped cleanups (`LockGuardCleanup`).
- All held locks are automatically released (`releaseAllLocks()`) on transaction `COMMIT`, `ROLLBACK`, or failure.

---

## Deadlock Detection (Step 5)

PayLock's **Deadlock Detector** (`paylock::DeadlockDetector`) identifies the Coffman **Circular Wait** condition using directed graph analysis:

### 1. Wait-For Graph (WFG) Construction
- **Vertices**: Active transactions currently executing or waiting.
- **Directed Edges**: An edge $T_1 \rightarrow T_2$ signifies that transaction $T_1$ is blocked waiting for a lock held by transaction $T_2$.
- **Snapshot Isolation**: Extracted under atomic mutex protection (`LockManager::getSnapshot()`) so graph operations never block running transactions.
- **Dependency Multi-Branching**: If an exclusive request waits on an account held by multiple shared-lock holders, edges are created to every conflicting owner.

### 2. Cycle Detection via 3-Color DFS
- Implements Depth-First Search with **WHITE (0, unvisited)**, **GRAY (1, in-stack)**, and **BLACK (2, processed)** states.
- Back-edges to GRAY vertices indicate directed cycles, extracted as closed paths ($T_1 \rightarrow T_2 \rightarrow T_1$).
- **Cycle Canonicalization**: Cycles are normalized to start from the minimum transaction ID while preserving directed edge order, eliminating duplicate cycle reports.
- **Time Complexity**: Optimal $O(V + E)$ time and space complexity.

### 3. Safe Controlled Demonstration
- Deterministically orchestrates two concurrent transactions ($T_1$ and $T_2$) competing across accounts `A101` and `A102` without arbitrary sleeps.
- Safely terminates by coordinated unlocking without performing premature transaction recovery.

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
Executes all 51 unit, integration, ACID, concurrency, architecture, Lock Manager, and Deadlock Detection tests.

### 3. Open the Frontend Dashboard
```bash
# Option 1: Direct browser launch
open frontend/index.html

# Option 2: Local HTTP server
python3 -m http.server 8080 --directory frontend
```

---

## Future Modules
In upcoming phases, the remaining advanced modules will be populated with full implementations:
- **Lock Manager**: **[COMPLETED IN STEP 4]** Granular row-level 2-Phase Locking (2PL), shared read locks, exclusive write locks, FIFO wait queues, lock upgrades.
- **Deadlock Detection**: **[COMPLETED IN STEP 5]** Wait-For Graph (WFG) snapshot synthesis, 3-color DFS directed cycle detection, DeadlockReport generation, controlled demonstration.
- **Deadlock Recovery (Step 6)**: Automated victim selection (Youngest Transaction, Lowest Cost, Fewest Locks), transaction abort, lock release, and automatic client retry.
- **Resource Management**: Operating systems paging simulation, memory frame allocation algorithms (FIFO, LRU), and resource request/allocation matrices.
- **REST API Layer**: Embedded C++ HTTP server bridging the C++ backend to the web dashboard via standard JSON endpoints (`GET /api/accounts`, `POST /api/transfer`, `GET /api/deadlocks`).
- **Frontend Integration**: Full live bi-directional dashboard synchronization with real-time WebSocket or Server-Sent Events (SSE) telemetry.
