# PayLock Database Design & Specification

## 1. Database Architecture
- **DBMS**: MySQL 8.x / 9.x
- **Storage Engine**: InnoDB (ACID compliant, row-level locking, foreign key integrity, WAL redo log)
- **Database Name**: `paylock_db`
- **Driver**: MySQL C/C++ Client Library (`libmysqlclient` / `<mysql.h>`)

---

## 2. Relational Schema & Tables

### 2.1 `users` Table
Stores account holder identity and registration metadata.
```sql
CREATE TABLE users (
    user_id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(150) NOT NULL UNIQUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
```

### 2.2 `accounts` Table
Stores ledger account balances with domain consistency constraints.
```sql
CREATE TABLE accounts (
    account_id INT PRIMARY KEY,
    user_id INT NOT NULL,
    balance DECIMAL(15, 2) NOT NULL DEFAULT 0.00,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_accounts_user FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    CONSTRAINT chk_account_balance CHECK (balance >= 0.00)
) ENGINE=InnoDB;
```
- **Monetary Precision**: Uses exact `DECIMAL(15, 2)` arithmetic to eliminate binary floating-point rounding errors. In C++, balances are verified using integer cents (`int64_t toCents(double)`) for lossless comparisons.
- **Integrity**: `chk_account_balance` prevents account overdrafts directly at the storage engine level.

### 2.3 `transactions` Table
Maintains financial transaction records and their explicit lifecycle states.
```sql
CREATE TABLE transactions (
    transaction_id INT AUTO_INCREMENT PRIMARY KEY,
    sender_account INT NULL,
    receiver_account INT NULL,
    amount DECIMAL(15, 2) NOT NULL,
    status ENUM('PENDING', 'ACTIVE', 'PROCESSING', 'COMMITTED', 'ROLLED_BACK', 'FAILED') NOT NULL DEFAULT 'PENDING',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_tx_sender FOREIGN KEY (sender_account) REFERENCES accounts(account_id),
    CONSTRAINT fk_tx_receiver FOREIGN KEY (receiver_account) REFERENCES accounts(account_id),
    CONSTRAINT chk_tx_amount CHECK (amount > 0.00),
    CONSTRAINT chk_tx_party CHECK (sender_account IS NOT NULL OR receiver_account IS NOT NULL)
) ENGINE=InnoDB;
```
- **Nullable Account IDs**: Allows external cash deposits (`sender_account = NULL`) and cash withdrawals (`receiver_account = NULL`) while enforcing foreign keys on all account-to-account transfers.

### 2.4 `transaction_logs` Table
Write-Ahead Logging (WAL) and audit trail for DBMS transaction auditing and recovery.
```sql
CREATE TABLE transaction_logs (
    log_id INT AUTO_INCREMENT PRIMARY KEY,
    transaction_id INT NOT NULL,
    operation VARCHAR(50) NOT NULL,
    message TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_logs_transaction FOREIGN KEY (transaction_id) REFERENCES transactions(transaction_id) ON DELETE CASCADE
) ENGINE=InnoDB;
```

---

## 3. Account Balance Handling & Representation
- **Database Level**: `DECIMAL(15, 2)` represents values up to ₹9,999,999,999,999.99 with exact 2-decimal precision.
- **Application Level (C++)**: To prevent floating-point imprecision when evaluating conditions like `balance >= amount`, amounts are converted to integer cents:
  ```cpp
  int64_t toCents(double amount) {
      return std::llround(amount * 100.0);
  }
  ```
- **Defense in Depth**:
  1. Application checks `toCents(balance) >= toCents(amount)`.
  2. SQL query executes `UPDATE accounts SET balance = balance - amount WHERE account_id = ?`.
  3. MySQL InnoDB constraint `CHECK (balance >= 0.00)` acts as an ironclad safeguard against accidental negative balances.

---

## 4. SQL Transaction Flow, COMMIT & ROLLBACK

All financial modifications in `TransactionManager` execute inside an explicit atomic MySQL transaction block:

```text
                  Client Request: transfer(A, B, amount)
                                    │
                                    ▼
                         Pre-Flight Validation
                     - Amount > 0 ?
                     - Sender != Receiver ?
                     - Sender and Receiver Exist ?
                     - Sender Balance >= Amount ?
                       /                         \
                     NO                          YES
                     │                            │
                     ▼                            ▼
             Abort & Log Failure          START TRANSACTION;
             (No DB Balance Change)               │
                                                  ▼
                                      INSERT INTO transactions
                                         (Status: 'ACTIVE')
                                                  │
                                                  ▼
                                         INSERT INTO logs
                                       ('TRANSFER_STARTED')
                                                  │
                                                  ▼
                                        UPDATE accounts (Debit A)
                                                  │
                                                  ▼
                                         INSERT INTO logs
                                     ('SENDER_BALANCE_DEBITED')
                                                  │
                                                  ▼
                                        UPDATE accounts (Credit B)
                                                  │
                                                  ▼
                                         INSERT INTO logs
                                    ('RECEIVER_BALANCE_CREDITED')
                                                  │
                                                  ▼
                                      UPDATE transactions
                                       (Status: 'COMMITTED')
                                                  │
                                                  ▼
                                         INSERT INTO logs
                                      ('TRANSACTION_COMMITTED')
                                                  │
                                       ┌──────────┴──────────┐
                                       ▼                     ▼
                                    Success?             Exception?
                                       │                     │
                                       ▼                     ▼
                                    COMMIT;               ROLLBACK;
```

---

## 5. Explicit Transaction States & Transitions

Transactions follow strict state transitions:

```text
                ┌─────────┐
                │ PENDING │
                └────┬────┘
                     │ (Transaction record created in DB)
                     ▼
                ┌─────────┐
                │ ACTIVE  │ (or PROCESSING)
                └────┬────┘
                     │
       ┌─────────────┼─────────────┐
       ▼             ▼             ▼
┌───────────┐ ┌─────────────┐ ┌────────┐
│ COMMITTED │ │ ROLLED_BACK │ │ FAILED │
└───────────┘ └─────────────┘ └────────┘
```

- **Terminal States**: Once a transaction reaches `COMMITTED`, `ROLLED_BACK`, or `FAILED`, no further status transitions are permitted.
- **`COMMITTED`**: All operations (debit, credit, log updates) successfully completed and durably written via `COMMIT`.
- **`ROLLED_BACK`**: An unexpected exception or failure occurred after `START TRANSACTION`; MySQL `ROLLBACK` was executed, leaving all account balances completely unchanged.
- **`FAILED`**: The transaction was rejected before debit execution (e.g. insufficient balance or invalid account).

---

## 6. Transaction Logging Architecture

PayLock employs a dual logging mechanism:
1. **Centralized Thread-Safe Logger (C++ Memory)**:
   - Synchronized via `std::mutex` across all worker threads.
   - Outputs color-coded and timestamped `[INFO]`, `[WARNING]`, `[ERROR]`, and `[DEBUG]` traces to the system console.
2. **Persistent Write-Ahead Log (`transaction_logs` in MySQL)**:
   - Stores each discrete operation (`TRANSFER_STARTED`, `SENDER_BALANCE_DEBITED`, `RECEIVER_BALANCE_CREDITED`, `TRANSACTION_COMMITTED`, `TRANSACTION_ROLLED_BACK`, `TRANSFER_FAILED`).
   - Retains a complete audit trail for DBMS auditing, debugging, and recovery.

---

## 7. ACID Guarantees in PayLock

| Property | Implementation & Guarantee |
|---|---|
| **Atomicity** | Either all operations (debit, credit, status update) succeed together, or `ROLLBACK` completely discards uncommitted changes. Verified via `demonstrateControlledRollback()`. |
| **Consistency** | Relational foreign keys and `CHECK (balance >= 0.00)` guarantee database consistency before and after transaction execution. |
| **Isolation** | Database transactions execute within MySQL InnoDB isolation (READ COMMITTED / REPEATABLE READ), preventing dirty reads. In Step 3+, application-level concurrency control (`std::mutex`, `LockManager`) guards against concurrent race conditions. |
| **Durability** | Upon `COMMIT`, changes are flushed to MySQL's Write-Ahead Log (WAL) and InnoDB tablespace files on disk, surviving crashes and reboots. |
