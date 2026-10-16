-- ============================================================================
-- PayLock Seed Data
-- Day 1: Baseline Seed Records for Verification
-- ============================================================================

USE paylock_db;

-- Clear previous data if any
DELETE FROM transaction_logs;
DELETE FROM transactions;
DELETE FROM accounts;
DELETE FROM users;

-- Reset Auto Increment counters
ALTER TABLE users AUTO_INCREMENT = 1;
ALTER TABLE transactions AUTO_INCREMENT = 1001;
ALTER TABLE transaction_logs AUTO_INCREMENT = 1;

-- 1. Insert Initial Users
INSERT INTO users (user_id, name, email) VALUES
(1, 'Nitin', 'nitin@paylock.local'),
(2, 'Ansh', 'ansh@paylock.local'),
(3, 'Megha', 'megha@paylock.local'),
(4, 'Vedika', 'vedika@paylock.local'),
(5, 'Rohan', 'rohan@paylock.local');

-- 2. Insert Initial Accounts with Specified Balances
-- Nitin   -> Account 101 -> ₹10,000
-- Ansh    -> Account 102 -> ₹5,000
-- Megha   -> Account 103 -> ₹7,000
-- Vedika  -> Account 104 -> ₹15,000
-- Rohan   -> Account 105 -> ₹12,000
INSERT INTO accounts (account_id, user_id, balance) VALUES
(101, 1, 10000.00),
(102, 2, 5000.00),
(103, 3, 7000.00),
(104, 4, 15000.00),
(105, 5, 12000.00);

