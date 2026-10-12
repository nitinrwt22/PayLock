# PayLock Database Design & Specification

## Database Architecture
- **DBMS**: MySQL 8.x / 9.x
- **Storage Engine**: InnoDB (ACID compliant, row-level locking, foreign key integrity)
- **Database Name**: `paylock_db`

---

## Relational Schema

### 1. `users` Table
Stores account holder identity and metadata.
```sql
CREATE TABLE users (
    user_id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(150) NOT NULL UNIQUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;
```

### 2. `accounts` Table
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
- **Monetary Precision**: Uses `DECIMAL(15, 2)` to avoid floating-point binary representation errors.
- **Integrity**: `chk_account_balance` prevents account overdrafts at the storage engine level.

### 3. `transactions` Table
Maintains financial transaction records and their lifecycle states.
```sql
CREATE TABLE transactions (
    transaction_id INT AUTO_INCREMENT PRIMARY KEY,
    sender_account INT NULL,
    receiver_account INT NULL,
    amount DECIMAL(15, 2) NOT NULL,
    status ENUM('PENDING', 'PROCESSING', 'COMMITTED', 'ROLLED_BACK', 'FAILED') NOT NULL DEFAULT 'PENDING',
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_tx_sender FOREIGN KEY (sender_account) REFERENCES accounts(account_id),
    CONSTRAINT fk_tx_receiver FOREIGN KEY (receiver_account) REFERENCES accounts(account_id),
    CONSTRAINT chk_tx_amount CHECK (amount > 0.00),
    CONSTRAINT chk_tx_party CHECK (sender_account IS NOT NULL OR receiver_account IS NOT NULL)
) ENGINE=InnoDB;
```
- **Nullable Account IDs**: Allows external cash deposits (`sender_account = NULL`) and cash withdrawals (`receiver_account = NULL`) while enforcing foreign keys on all account-to-account transfers.

### 4. `transaction_logs` Table
Write-Ahead Logging (WAL) and audit trail for DBMS transaction recovery.
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

## Transaction Lifecycle in PayLock

```text
       [Client Initiates Transfer]
                   │
                   ▼
       START TRANSACTION;
                   │
                   ▼
       UPDATE accounts (Debit Sender)
                   │
                   ▼
       UPDATE accounts (Credit Receiver)
                   │
                   ▼
       INSERT INTO transactions (Status: 'COMMITTED')
                   │
                   ▼
       INSERT INTO transaction_logs (WAL Audit Trail)
                   │
       ┌───────────┴───────────┐
       ▼                       ▼
    [Success]               [Failure]
    COMMIT;                 ROLLBACK;
```
