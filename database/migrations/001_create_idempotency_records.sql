-- ============================================================================
-- Migration 001: Create Idempotency Records Table
-- Task 4: Persistent Idempotency for Payment Operations
-- ============================================================================

USE paylock_db;

CREATE TABLE IF NOT EXISTS idempotency_records (
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
