-- ============================================================================
-- PayLock Database Schema
-- Day 1: Foundational Database Schema
-- Concurrency & Transaction Management System
-- ============================================================================

CREATE DATABASE IF NOT EXISTS paylock_db;
USE paylock_db;

-- Drop tables in reverse order of foreign key dependencies
DROP TABLE IF EXISTS idempotency_records;
DROP TABLE IF EXISTS transaction_logs;
DROP TABLE IF EXISTS transactions;
DROP TABLE IF EXISTS accounts;
DROP TABLE IF EXISTS users;

-- ----------------------------------------------------------------------------
-- 1. Users Table
-- Stores user identity information.
-- ----------------------------------------------------------------------------
CREATE TABLE users (
    user_id INT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(100) NOT NULL,
    email VARCHAR(150) NOT NULL UNIQUE,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- 2. Accounts Table
-- DBMS Concept: Consistency & Integrity Constraints
-- - Uses DECIMAL(15, 2) for exact financial precision without floating point inaccuracies.
-- - CHECK constraint ensures balances never drop below zero.
-- ----------------------------------------------------------------------------
CREATE TABLE accounts (
    account_id INT PRIMARY KEY,
    user_id INT NOT NULL,
    balance DECIMAL(15, 2) NOT NULL DEFAULT 0.00,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_accounts_user FOREIGN KEY (user_id) REFERENCES users(user_id) ON DELETE CASCADE,
    CONSTRAINT chk_account_balance CHECK (balance >= 0.00)
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- 3. Transactions Table
-- DBMS Concept: Atomicity & Transaction State Tracking
-- - Tracks the lifecycle of financial transfers between two accounts.
-- - CHECK constraint enforces strictly positive transfer amounts.
-- ----------------------------------------------------------------------------
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

-- ----------------------------------------------------------------------------
-- 4. Transaction Logs Table
-- DBMS Concept: Durability, Audit Trail & Write-Ahead Logging (WAL)
-- - Captures individual operations (locks acquired, debits, credits, commits, rollbacks)
-- - Provides auditing and recovery trace for DBMS operations.
-- ----------------------------------------------------------------------------
CREATE TABLE transaction_logs (
    log_id INT AUTO_INCREMENT PRIMARY KEY,
    transaction_id INT NOT NULL,
    operation VARCHAR(50) NOT NULL,
    message TEXT,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    CONSTRAINT fk_logs_transaction FOREIGN KEY (transaction_id) REFERENCES transactions(transaction_id) ON DELETE CASCADE
) ENGINE=InnoDB;

-- ----------------------------------------------------------------------------
-- 5. Idempotency Records Table
-- DBMS Concept: At-most-once semantics, Exactly-Once Execution, State Persistence
-- - Ensures mutating payment operations (transfer, deposit, withdraw) are safely idempotent.
-- - Fingerprinted by canonical operation payload hash to detect payload conflicts.
-- - Tracks atomic lifecycle state (PROCESSING -> COMMITTED / FAILED).
-- ----------------------------------------------------------------------------
CREATE TABLE idempotency_records (
    idempotency_key VARCHAR(128) PRIMARY KEY,
    operation_type VARCHAR(32) NOT NULL,
    request_hash VARCHAR(64) NOT NULL,
    request_payload TEXT NOT NULL,
    status ENUM('PROCESSING', 'COMMITTED', 'FAILED') NOT NULL DEFAULT 'PROCESSING',
    response_code INT NOT NULL DEFAULT 0,
    response_body MEDIUMTEXT NULL,
    transaction_id INT NULL,
    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    CONSTRAINT fk_idempotency_tx FOREIGN KEY (transaction_id) REFERENCES transactions(transaction_id) ON DELETE SET NULL,
    INDEX idx_idempotency_created (created_at)
) ENGINE=InnoDB;

