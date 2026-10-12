-- ============================================================================
-- PayLock Database Stored Procedures & Triggers
-- Future Extension: Database Stored Procedures for Encapsulated Transactions
-- ============================================================================

USE paylock_db;

DELIMITER //

-- ----------------------------------------------------------------------------
-- 1. Transfer Funds Stored Procedure (Database-level Atomic Transaction)
-- ----------------------------------------------------------------------------
DROP PROCEDURE IF EXISTS sp_transfer_funds //
CREATE PROCEDURE sp_transfer_funds(
    IN p_sender_id INT,
    IN p_receiver_id INT,
    IN p_amount DECIMAL(15, 2),
    OUT p_status VARCHAR(50),
    OUT p_tx_id INT
)
proc_body: BEGIN
    DECLARE v_sender_balance DECIMAL(15, 2);
    
    -- Error Handler for Rollback
    DECLARE EXIT HANDLER FOR SQLEXCEPTION
    BEGIN
        ROLLBACK;
        SET p_status = 'ROLLED_BACK_ERROR';
        SET p_tx_id = 0;
    END;

    -- Pre-validation
    IF p_amount <= 0.00 THEN
        SET p_status = 'INVALID_AMOUNT';
        SET p_tx_id = 0;
        LEAVE proc_body;
    END IF;

    IF p_sender_id = p_receiver_id THEN
        SET p_status = 'IDENTICAL_ACCOUNTS';
        SET p_tx_id = 0;
        LEAVE proc_body;
    END IF;

    -- Start Atomic Transaction
    START TRANSACTION;

    -- Check sender solvency
    SELECT balance INTO v_sender_balance 
    FROM accounts 
    WHERE account_id = p_sender_id 
    FOR UPDATE;

    IF v_sender_balance IS NULL THEN
        ROLLBACK;
        SET p_status = 'SENDER_NOT_FOUND';
        SET p_tx_id = 0;
        LEAVE proc_body;
    END IF;

    IF v_sender_balance < p_amount THEN
        ROLLBACK;
        SET p_status = 'INSUFFICIENT_FUNDS';
        -- Record audit attempt outside transaction
        INSERT INTO transactions (sender_account, receiver_account, amount, status)
        VALUES (p_sender_id, p_receiver_id, p_amount, 'ROLLED_BACK');
        SET p_tx_id = LAST_INSERT_ID();
        INSERT INTO transaction_logs (transaction_id, operation, message)
        VALUES (p_tx_id, 'TRANSFER_FAILED', 'Insufficient funds in sender account.');
        LEAVE proc_body;
    END IF;

    -- Debit & Credit
    UPDATE accounts SET balance = balance - p_amount WHERE account_id = p_sender_id;
    UPDATE accounts SET balance = balance + p_amount WHERE account_id = p_receiver_id;

    -- Record transaction
    INSERT INTO transactions (sender_account, receiver_account, amount, status)
    VALUES (p_sender_id, p_receiver_id, p_amount, 'COMMITTED');
    SET p_tx_id = LAST_INSERT_ID();

    -- Write-Ahead Logging
    INSERT INTO transaction_logs (transaction_id, operation, message)
    VALUES 
    (p_tx_id, 'TRANSFER_STARTED', CONCAT('Initiated transfer of ₹', p_amount)),
    (p_tx_id, 'SENDER_BALANCE_DEBITED', CONCAT('Debited ₹', p_amount, ' from Account ', p_sender_id)),
    (p_tx_id, 'RECEIVER_BALANCE_CREDITED', CONCAT('Credited ₹', p_amount, ' to Account ', p_receiver_id)),
    (p_tx_id, 'TRANSACTION_COMMITTED', 'Stored procedure executed and committed successfully.');

    COMMIT;
    SET p_status = 'COMMITTED';
END //

DELIMITER ;
