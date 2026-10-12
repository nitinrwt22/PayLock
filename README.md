# PayLock: A Concurrent Payment Transaction Management System

## Project Objective
**PayLock** is a high-performance payment transaction management system designed to demonstrate core principles of **Operating Systems (OS)** and **Database Management Systems (DBMS)** within a unified C++ and MySQL architecture.

The project demonstrates how concurrent financial transactions are processed safely, preventing race conditions, adhering to ACID transaction guarantees, and maintaining data consistency across accounts.

---

## Key Concepts

### Operating Systems (OS) Concepts
- **Concurrency & Multithreading (Day 3+)**: Worker threads concurrently processing transactions.
- **Mutual Exclusion & Locks (Day 3+)**: Thread synchronization using mutexes (`std::mutex`) and lock guards (`std::lock_guard`).
- **Deadlock Detection & Handling (Day 4+)**: Wait-for graph (WFG) cycle detection and victim selection for rollback.
- **Resource Management & Synchronization (Day 1+)**: RAII for safe connection handle destruction and mutex-protected query execution.

### DBMS Concepts
- **ACID Transactions**:
  - **Atomicity**: Transfers are all-or-nothing units of work (`START TRANSACTION`, `COMMIT`, `ROLLBACK`).
  - **Consistency**: Relational integrity and domain constraints (`CHECK (balance >= 0)`, `CHECK (amount > 0)`).
  - **Isolation**: Serializability and row-level locking to avoid race conditions and dirty reads.
  - **Durability**: Persistent storage backed by MySQL InnoDB engine.
- **Write-Ahead Logging / Audit Trail**: Recording every lifecycle operation (`transaction_logs` table).

---

---

## Development Milestones

### Day 1 Completed (Foundational Infrastructure)
- C++ project structure and build configuration (CMake & Make).
- C++ ↔ MySQL connectivity via `libmysqlclient`.
- Database initialization (`paylock_db`).
- Schema definition (`users`, `accounts`, `transactions`, `transaction_logs`) with DECIMAL currency precision and CHECK constraints.
- Seed data (`Nitin`: ₹10,000, `Ansh`: ₹5,000, `Megha`: ₹7,000).
- Modular architecture with `DatabaseManager`, `TransactionManager`, and domain models.

### Day 2 Completed (Core Payment Transaction System)
- **Core Payment Business Logic**:
  - `transfer(senderAccountId, receiverAccountId, amount)`: Atomic peer-to-peer fund transfers.
  - `deposit(accountId, amount)`: External cash/system credit into an account.
  - `withdraw(accountId, amount)`: Controlled debit preventing overdrafts.
- **MySQL Transactions & ACID Foundation**:
  - Strict transaction lifecycle management (`START TRANSACTION;` -> Operations -> `COMMIT;` / `ROLLBACK;`).
  - Balance updates, transaction records, and audit logs executed within the same atomic database transaction.
  - No autocommit for financial transfers.
- **Transaction Status Lifecycle**:
  - `PENDING` -> `PROCESSING` -> `COMMITTED` (successful) or `ROLLED_BACK` / `FAILED` (aborted).
- **Write-Ahead Audit Logging (`transaction_logs`)**:
  - Real-time logging of `TRANSFER_STARTED`, `SENDER_BALANCE_DEBITED`, `RECEIVER_BALANCE_CREDITED`, `TRANSACTION_COMMITTED`, `TRANSACTION_ROLLED_BACK`, and `TRANSFER_FAILED`.
- **Controlled Failure & Atomicity Demonstration**:
  - Demonstrated that overdrawn transfers (e.g. ₹15,000 transfer from an account holding ₹8,000) are cleanly rolled back, leaving balances completely untouched.
- **Safe Database Practices**:
  - Exact `DECIMAL(15, 2)` monetary calculations without floating-point inaccuracies.
  - Query parameterization and input escaping against injection.
  - Defense-in-depth: Application balance pre-validation + MySQL `CHECK (balance >= 0.00)` constraint enforcement.

---

## How PayLock Handles a Payment

The payment workflow illustrates the classic DBMS transaction processing pipeline:

```text
               C++ Application
                      │
                      ▼
             TransactionManager
                      │
           [1. Pre-Validation]
           - Amount > 0
           - Sender != Receiver
           - Sender & Receiver exist
           - Sender balance >= Amount
                      │
                      ▼
           [2. START TRANSACTION]
                      │
                      ▼
          [3. INSERT Transaction] ────► Status: 'PROCESSING'
                      │
                      ▼
           [4. Log: TRANSFER_STARTED]
                      │
                      ▼
           [5. UPDATE Sender Balance] ──► Deduct Amount (CHECK balance >= 0)
                      │
                      ▼
        [6. Log: SENDER_BALANCE_DEBITED]
                      │
                      ▼
         [7. UPDATE Receiver Balance] ──► Add Amount
                      │
                      ▼
       [8. Log: RECEIVER_BALANCE_CREDITED]
                      │
                      ▼
             Everything Succeeded?
                    /       \
                  YES       NO (Validation / DB Error)
                  /           \
                 ▼             ▼
       [UPDATE Status]     [ROLLBACK] ──► Discards all partial balance updates
       Status: 'COMMITTED'     │
                 │             ▼
                 ▼      [INSERT Failure Audit]
       [Log: COMMITTED] Status: 'ROLLED_BACK'
                 │      Log: 'TRANSACTION_ROLLED_BACK'
                 ▼             │
             [COMMIT]          ▼
                 │        Return False
                 ▼
            Return True
```

---

## ACID Properties in PayLock

| Property | Implementation in PayLock |
|---|---|
| **Atomicity** | Money deduction and credit occur inside a single MySQL transaction. If any step fails (e.g. insufficient balance, network failure, or constraint violation), `ROLLBACK` undoes all partial updates, ensuring an "all-or-nothing" execution. |
| **Consistency** | Database integrity is maintained before and after transactions via relational foreign keys (`fk_tx_sender`, `fk_tx_receiver`) and domain constraints (`chk_account_balance`, `chk_tx_amount`). |
| **Isolation** | Single-threaded baseline established for Day 2. Full concurrent isolation levels, row-level locks, and thread synchronization will be implemented on Day 3. |
| **Durability** | Once `COMMIT` executes, all state changes are permanently flushed to disk via MySQL InnoDB's Write-Ahead Log (WAL) and redo logs. |

---

## Tech Stack
- **Language**: C++ (C++17 / Apple Clang / GCC)
- **Database**: MySQL 9.x (InnoDB Engine)
- **Database Driver**: MySQL C/C++ Client Library (`libmysqlclient` / `<mysql.h>`)
- **Build System**: CMake (v3.15+) and Make

---

## Project Structure
```
PayLock/
├── CMakeLists.txt              # CMake build definition
├── Makefile                    # Makefile for quick build & test
├── README.md                   # Documentation
├── docs/
│   └── architecture.md         # Architecture specification
├── sql/
│   ├── schema.sql              # Database schema & constraints
│   └── seed.sql                # Seed data for testing
├── src/
│   ├── main.cpp                # Terminal demonstration entry point
│   ├── database/
│   │   ├── DatabaseManager.h   # MySQL connection & query interface
│   │   └── DatabaseManager.cpp # Implementation with RAII & mutex locking
│   ├── models/
│   │   ├── Account.h           # Account model
│   │   ├── Transaction.h       # Transaction model
│   │   ├── TransactionLog.h    # Transaction log model
│   │   └── User.h              # User model
│   └── transaction/
│       ├── TransactionManager.h   # High-level transaction manager
│       └── TransactionManager.cpp # Business orchestration
└── tests/
    └── test_db.cpp             # Unit & constraint verification tests
```

---

## Database Schema

### 1. `users` Table
| Column | Type | Constraints | Description |
|---|---|---|---|
| `user_id` | `INT` | `PRIMARY KEY, AUTO_INCREMENT` | Unique identifier for each user |
| `name` | `VARCHAR(100)` | `NOT NULL` | User full name |
| `email` | `VARCHAR(150)` | `NOT NULL, UNIQUE` | Unique email address |
| `created_at` | `TIMESTAMP` | `DEFAULT CURRENT_TIMESTAMP` | Registration timestamp |

### 2. `accounts` Table
| Column | Type | Constraints | Description |
|---|---|---|---|
| `account_id` | `INT` | `PRIMARY KEY` | Account number (e.g. 101, 102) |
| `user_id` | `INT` | `NOT NULL, FK -> users(user_id)` | Account owner |
| `balance` | `DECIMAL(15, 2)` | `NOT NULL, CHECK (balance >= 0.00)` | Non-negative account balance |
| `created_at` | `TIMESTAMP` | `DEFAULT CURRENT_TIMESTAMP` | Account creation timestamp |

### 3. `transactions` Table
| Column | Type | Constraints | Description |
|---|---|---|---|
| `transaction_id` | `INT` | `PRIMARY KEY, AUTO_INCREMENT` | Unique transaction ID (starts at 1001) |
| `sender_account` | `INT` | `NULL, FK -> accounts(account_id)` | Sender account ID (NULL / 0 = cash deposit) |
| `receiver_account` | `INT` | `NULL, FK -> accounts(account_id)` | Receiver account ID (NULL / 0 = withdrawal) |
| `amount` | `DECIMAL(15, 2)` | `NOT NULL, CHECK (amount > 0.00)` | Transfer amount (must be positive) |
| `status` | `ENUM` | `'PENDING', 'PROCESSING', 'COMMITTED', 'ROLLED_BACK', 'FAILED'` | Transaction state |
| `created_at` | `TIMESTAMP` | `DEFAULT CURRENT_TIMESTAMP` | Timestamp of initiation |

### 4. `transaction_logs` Table
| Column | Type | Constraints | Description |
|---|---|---|---|
| `log_id` | `INT` | `PRIMARY KEY, AUTO_INCREMENT` | Unique log entry ID |
| `transaction_id` | `INT` | `NOT NULL, FK -> transactions(transaction_id)` | Associated transaction |
| `operation` | `VARCHAR(50)` | `NOT NULL` | Operation type (e.g., SENDER_BALANCE_DEBITED) |
| `message` | `TEXT` | `NULL` | Informational audit trace |
| `created_at` | `TIMESTAMP` | `DEFAULT CURRENT_TIMESTAMP` | Log record timestamp |

---

## How to Configure MySQL

### 1. Apply Schema and Seed Data
Run the SQL scripts using the MySQL command line client:
```bash
mysql -u root -p < sql/schema.sql
mysql -u root -p < sql/seed.sql
```

### 2. Configure Environment Variables (Optional)
The application connects with sensible local defaults (`host=127.0.0.1`, `user=root`, `database=paylock_db`, `port=3306`). You can customize the connection credentials via environment variables:
```bash
export PAYLOCK_DB_HOST="127.0.0.1"
export PAYLOCK_DB_USER="root"
export PAYLOCK_DB_PASS="your_password"
export PAYLOCK_DB_NAME="paylock_db"
export PAYLOCK_DB_PORT="3306"
```

---

## How to Build

### Option A: Using Make (Recommended for fast build)
```bash
# Build main binary and test suite
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

## How to Run

### Run Main Demonstration:
```bash
# Direct binary execution
./paylock

# Or via make
make run
```

### Run Test Suite:
```bash
# Direct binary execution
./test_db

# Or via make
make test
```

---

## Example Output

### Running `./paylock`:
```text
==================================================
        PAYLOCK PAYMENT TRANSACTION SYSTEM        
   Day 2: Core ACID Transactions & DBMS Engine    
==================================================

[Database]: Connected to MySQL successfully.

Initial Account State:
------------------------------------------------
Account 101 (Nitin): ₹10000
Account 102 (Ansh): ₹5000
Account 103 (Megha): ₹7000

>>> TEST 1: Transfer ₹2,000 from Account 101 -> Account 102
Result    : SUCCESS (COMMITTED)
Balances  : Account 101 = ₹8000 | Account 102 = ₹7000
Validation: PASSED

>>> TEST 2: Transfer ₹15,000 from Account 101 -> Account 103 (Insufficient Funds)
Result    : REJECTED & ROLLED_BACK (As expected)
Reason    : Insufficient funds in Account 101 (Available: ₹8000.00, Requested: ₹15000.00)
Balances  : Account 101 remains ₹8000 | Account 103 remains ₹7000
Validation: PASSED (Atomicity preserved)

>>> TEST 3: Deposit ₹1,000 into Account 102
Result    : SUCCESS (COMMITTED)
Balances  : Account 102 = ₹8000
Validation: PASSED

>>> TEST 4: Withdraw ₹2,000 from Account 103
Result    : SUCCESS (COMMITTED)
Balances  : Account 103 = ₹5000
Validation: PASSED

>>> TEST 5: Withdraw ₹10,000 from Account 103 (Exceeds ₹5,000 Balance)
Result    : REJECTED & ROLLED_BACK (As expected)
Reason    : Insufficient funds in Account 103 (Available: ₹5000.00, Requested: ₹10000.00)
Balances  : Account 103 remains ₹5000
Validation: PASSED (Integrity preserved)

========================================
            ACCOUNT BALANCES
========================================
Account 101 (Nitin ) : ₹8000.00
Account 102 (Ansh  ) : ₹8000.00
Account 103 (Megha ) : ₹5000.00
========================================

========================================
          PAYLOCK TRANSACTIONS
========================================

ID     FROM    TO      AMOUNT       STATUS
------------------------------------------------
1001   101     102     ₹2000      COMMITTED
1002   101     103     ₹15000     ROLLED_BACK
1003   0       102     ₹1000      COMMITTED
1004   103     0       ₹2000      COMMITTED
1005   103     0       ₹10000     ROLLED_BACK

========================================

========================================
     TRANSACTION AUDIT LOG (WAL TRACE)  
========================================

Transaction #1001
-------------------------------
TRANSFER_STARTED
Sender: 101
Receiver: 102
Amount: ₹2000.00

SENDER_BALANCE_DEBITED
Debited ₹2000.00 from Account 101

RECEIVER_BALANCE_CREDITED
Credited ₹2000.00 to Account 102

TRANSACTION_COMMITTED
Transfer completed and permanently committed to MySQL.

----------------------------------------
Controlled Failure Audit Log (#1002):
----------------------------------------
Transaction #1002
-------------------------------
TRANSFER_STARTED
Sender: 101
Receiver: 103
Amount: ₹15000.00

TRANSFER_FAILED
Insufficient funds in Account 101 (Available: ₹8000.00, Requested: ₹15000.00)

TRANSACTION_ROLLED_BACK
Transaction rolled back due to insufficient sender balance. Balances unchanged.

==================================================
          ACID PROPERTIES DEMONSTRATION           
==================================================
[ATOMICITY]   : Either all operations succeed or all are undone.
                Verified: Tx #1001 fully debited and credited.
                Verified: Tx #1002 insufficient balance rolled back completely;
                no partial deduction occurred.

[CONSISTENCY] : Schema constraints (balance >= 0.00, amount > 0.00)
                and foreign keys ensure database validity before and after.

[ISOLATION]   : Single-threaded baseline validated for Day 2.
                Full concurrent isolation with multithreading & locks
                will be demonstrated on Day 3.

[DURABILITY]  : Once COMMIT executes, changes are durably persisted
                to MySQL InnoDB transaction log (WAL).
==================================================
```

### Running `./test_db`:
```text
[TEST RUNNER] Starting Day 1 & Day 2 PayLock Automated Tests...
Test 1: Establishing connection... PASSED
Test 2: Verifying seed accounts count... PASSED (3 accounts found)
Test 3: Checking Account 101 balance... PASSED (₹8000)
Test 4: Verifying DBMS Constraint (Negative balance should fail)... PASSED (chk_account_balance enforced)
Test 5: Transfer ₹2,000 from 101 -> 102 (Happy Path)... PASSED (101=₹8000, 102=₹7000, COMMITTED)
Test 6: Transfer ₹15,000 from 101 -> 103 (Insufficient Balance)... PASSED (Rollback verified, balances intact)
Test 7: Transfer to non-existent account (Account 9999)... PASSED (Rejected without debiting sender)
Test 8: Deposit ₹1,000 into Account 102... PASSED (102=₹8000)
Test 9: Withdraw ₹2,000 from Account 103... PASSED (103=₹5000)
Test 10: Withdraw ₹10,000 from Account 103 (Overdraft attempt)... PASSED (Overdraft prevented, balance unchanged)
Test 11: Verifying WAL audit log entries exist... PASSED (35 log records verified)

[ALL 11 UNIT & INTEGRATION TESTS PASSED SUCCESSFULLY!]
```

