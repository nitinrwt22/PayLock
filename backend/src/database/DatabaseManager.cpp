#include "database/DatabaseManager.h"
#include <iostream>
#include <cstdlib>
#include <sstream>
#include <iomanip>

namespace paylock {

DatabaseConfig DatabaseConfig::fromEnvOrDefault() {
    DatabaseConfig cfg;
    if (const char* envHost = std::getenv("PAYLOCK_DB_HOST")) cfg.host = envHost;
    if (const char* envUser = std::getenv("PAYLOCK_DB_USER")) cfg.user = envUser;
    if (const char* envPass = std::getenv("PAYLOCK_DB_PASS")) cfg.password = envPass;
    if (const char* envDb   = std::getenv("PAYLOCK_DB_NAME")) cfg.database = envDb;
    if (const char* envPort = std::getenv("PAYLOCK_DB_PORT")) cfg.port = static_cast<unsigned int>(std::stoi(envPort));
    return cfg;
}

DatabaseManager::DatabaseManager()
    : config_(DatabaseConfig::fromEnvOrDefault()), conn_(nullptr) {}

DatabaseManager::DatabaseManager(DatabaseConfig config)
    : config_(std::move(config)), conn_(nullptr) {}

DatabaseManager::~DatabaseManager() {
    disconnect();
}

DatabaseManager::DatabaseManager(DatabaseManager&& other) noexcept {
    std::lock_guard<std::mutex> lock(other.dbMutex_);
    config_ = std::move(other.config_);
    conn_ = other.conn_;
    other.conn_ = nullptr;
}

DatabaseManager& DatabaseManager::operator=(DatabaseManager&& other) noexcept {
    if (this != &other) {
        disconnect();
        std::scoped_lock lock(dbMutex_, other.dbMutex_);
        config_ = std::move(other.config_);
        conn_ = other.conn_;
        other.conn_ = nullptr;
    }
    return *this;
}

void DatabaseManager::connect() {
    connect(config_);
}

void DatabaseManager::connect(const DatabaseConfig& config) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (conn_ != nullptr) {
        mysql_close(conn_);
        conn_ = nullptr;
    }

    config_ = config;
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        throw DatabaseException("Failed to initialize MySQL handler (mysql_init returned null)");
    }


    const char* socket = config_.socketPath.empty() ? nullptr : config_.socketPath.c_str();
    if (!mysql_real_connect(conn_,
                            config_.host.c_str(),
                            config_.user.c_str(),
                            config_.password.c_str(),
                            config_.database.c_str(),
                            config_.port,
                            socket,
                            0)) {
        std::string err = mysql_error(conn_);
        unsigned int errNo = mysql_errno(conn_);
        mysql_close(conn_);
        conn_ = nullptr;
        throw DatabaseException("MySQL Connection Failed: " + err, errNo);
    }

    mysql_set_character_set(conn_, "utf8mb4");
}

void DatabaseManager::disconnect() noexcept {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (conn_) {
        mysql_close(conn_);
        conn_ = nullptr;
    }
}

bool DatabaseManager::isConnected() noexcept {
    std::lock_guard<std::mutex> lock(dbMutex_);
    if (!conn_) return false;
    return (mysql_ping(conn_) == 0);
}

MYSQL* DatabaseManager::getRawConnection() noexcept {
    return conn_;
}

void DatabaseManager::ensureConnected() {
    if (!conn_ || mysql_ping(conn_) != 0) {
        throw DatabaseException("Database is not connected.");
    }
}

std::string DatabaseManager::escapeString(const std::string& str) {
    if (!conn_) return str;
    std::vector<char> buffer(str.length() * 2 + 1);
    mysql_real_escape_string(conn_, buffer.data(), str.c_str(), str.length());
    return std::string(buffer.data());
}

void DatabaseManager::executeQuery(const std::string& sql) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Query execution failed: " + std::string(mysql_error(conn_)) +
                                "\nSQL: " + sql, mysql_errno(conn_));
    }
}

int DatabaseManager::createUser(const std::string& name, const std::string& email) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string escapedName = escapeString(name);
    std::string escapedEmail = escapeString(email);

    std::string sql = "INSERT INTO users (name, email) VALUES ('" +
                      escapedName + "', '" + escapedEmail + "');";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to create user: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return static_cast<int>(mysql_insert_id(conn_));
}

bool DatabaseManager::createAccount(int accountId, int userId, double initialBalance) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "INSERT INTO accounts (account_id, user_id, balance) VALUES ("
       << accountId << ", " << userId << ", " << initialBalance << ");";

    std::string sql = ss.str();
    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to create account: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return true;
}

std::optional<double> DatabaseManager::getAccountBalance(int accountId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "SELECT balance FROM accounts WHERE account_id = " + std::to_string(accountId) + " LIMIT 1;";
    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to fetch balance: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        throw DatabaseException("Failed to store result: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<double> balance = std::nullopt;
    if (row && row[0]) {
        balance = std::stod(row[0]);
    }

    mysql_free_result(res);
    return balance;
}

std::optional<Account> DatabaseManager::getAccountById(int accountId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "SELECT a.account_id, a.user_id, u.name, a.balance, a.created_at "
                      "FROM accounts a "
                      "JOIN users u ON a.user_id = u.user_id "
                      "WHERE a.account_id = " + std::to_string(accountId) + " LIMIT 1;";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to get account: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        throw DatabaseException("Failed to store result: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<Account> account = std::nullopt;
    if (row) {
        int accId = std::stoi(row[0]);
        int uId = std::stoi(row[1]);
        std::string name = row[2] ? row[2] : "";
        double bal = row[3] ? std::stod(row[3]) : 0.0;
        std::string created = row[4] ? row[4] : "";
        account = Account(accId, uId, name, bal, created);
    }

    mysql_free_result(res);
    return account;
}

std::vector<Account> DatabaseManager::listAccounts() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "SELECT a.account_id, a.user_id, u.name, a.balance, a.created_at "
                      "FROM accounts a "
                      "JOIN users u ON a.user_id = u.user_id "
                      "ORDER BY a.account_id ASC;";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to list accounts: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        throw DatabaseException("Failed to store result: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    std::vector<Account> accounts;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        int accId = std::stoi(row[0]);
        int uId = std::stoi(row[1]);
        std::string name = row[2] ? row[2] : "";
        double bal = row[3] ? std::stod(row[3]) : 0.0;
        std::string created = row[4] ? row[4] : "";
        accounts.emplace_back(accId, uId, name, bal, created);
    }

    mysql_free_result(res);
    return accounts;
}

bool DatabaseManager::accountExists(int accountId) {
    return getAccountById(accountId).has_value();
}

std::vector<Transaction> DatabaseManager::listTransactions() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "SELECT transaction_id, sender_account, receiver_account, amount, status, created_at "
                      "FROM transactions "
                      "ORDER BY transaction_id ASC;";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to list transactions: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        throw DatabaseException("Failed to store result: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    std::vector<Transaction> transactions;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        int txId = std::stoi(row[0]);
        int sender = (row[1] != nullptr) ? std::stoi(row[1]) : 0;
        int receiver = (row[2] != nullptr) ? std::stoi(row[2]) : 0;
        double amount = row[3] ? std::stod(row[3]) : 0.0;
        std::string status = row[4] ? row[4] : "";
        std::string created = row[5] ? row[5] : "";
        transactions.emplace_back(txId, sender, receiver, amount, status, created);
    }

    mysql_free_result(res);
    return transactions;
}

std::vector<Transaction> DatabaseManager::getTransactionsForAccount(int accountId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "SELECT transaction_id, sender_account, receiver_account, amount, status, created_at "
                      "FROM transactions "
                      "WHERE sender_account = " + std::to_string(accountId) + " "
                      "   OR receiver_account = " + std::to_string(accountId) + " "
                      "ORDER BY transaction_id ASC;";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to get transactions for account " + std::to_string(accountId) + 
                                ": " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        throw DatabaseException("Failed to store result: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    std::vector<Transaction> transactions;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        int txId = std::stoi(row[0]);
        int sender = (row[1] != nullptr) ? std::stoi(row[1]) : 0;
        int receiver = (row[2] != nullptr) ? std::stoi(row[2]) : 0;
        double amount = row[3] ? std::stod(row[3]) : 0.0;
        std::string status = row[4] ? row[4] : "";
        std::string created = row[5] ? row[5] : "";
        transactions.emplace_back(txId, sender, receiver, amount, status, created);
    }

    mysql_free_result(res);
    return transactions;
}

int DatabaseManager::recordTransaction(int senderAccount, int receiverAccount, double amount, const std::string& status) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string senderStr = (senderAccount <= 0) ? "NULL" : std::to_string(senderAccount);
    std::string receiverStr = (receiverAccount <= 0) ? "NULL" : std::to_string(receiverAccount);

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "INSERT INTO transactions (sender_account, receiver_account, amount, status) VALUES ("
       << senderStr << ", " << receiverStr << ", " << amount << ", '" << escapeString(status) << "');";

    std::string sql = ss.str();
    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to record transaction: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return static_cast<int>(mysql_insert_id(conn_));
}

bool DatabaseManager::recordTransactionLog(int transactionId, const std::string& operation, const std::string& message) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "INSERT INTO transaction_logs (transaction_id, operation, message) VALUES ("
                      + std::to_string(transactionId) + ", '"
                      + escapeString(operation) + "', '"
                      + escapeString(message) + "');";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to record transaction log: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return true;
}

bool DatabaseManager::debitAccount(int accountId, double amount) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "UPDATE accounts SET balance = balance - " << amount 
       << " WHERE account_id = " << accountId << ";";

    std::string sql = ss.str();
    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to debit account " + std::to_string(accountId) + 
                                ": " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return mysql_affected_rows(conn_) > 0;
}

bool DatabaseManager::creditAccount(int accountId, double amount) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2);
    ss << "UPDATE accounts SET balance = balance + " << amount 
       << " WHERE account_id = " << accountId << ";";

    std::string sql = ss.str();
    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to credit account " + std::to_string(accountId) + 
                                ": " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return mysql_affected_rows(conn_) > 0;
}

bool DatabaseManager::updateTransactionStatus(int transactionId, const std::string& status) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "UPDATE transactions SET status = '" + escapeString(status) + 
                      "' WHERE transaction_id = " + std::to_string(transactionId) + ";";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to update transaction status: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return mysql_affected_rows(conn_) > 0;
}

std::optional<Transaction> DatabaseManager::getTransactionById(int transactionId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "SELECT transaction_id, sender_account, receiver_account, amount, status, created_at "
                      "FROM transactions WHERE transaction_id = " + std::to_string(transactionId) + " LIMIT 1;";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to fetch transaction: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        throw DatabaseException("Failed to store result: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<Transaction> tx = std::nullopt;
    if (row) {
        int txId = std::stoi(row[0]);
        int sender = (row[1] != nullptr) ? std::stoi(row[1]) : 0;
        int receiver = (row[2] != nullptr) ? std::stoi(row[2]) : 0;
        double amount = row[3] ? std::stod(row[3]) : 0.0;
        std::string status = row[4] ? row[4] : "";
        std::string created = row[5] ? row[5] : "";
        tx = Transaction(txId, sender, receiver, amount, status, created);
    }

    mysql_free_result(res);
    return tx;
}

std::vector<TransactionLog> DatabaseManager::listTransactionLogs(int transactionId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "SELECT log_id, transaction_id, operation, message, created_at FROM transaction_logs ";
    if (transactionId > 0) {
        sql += "WHERE transaction_id = " + std::to_string(transactionId) + " ";
    }
    sql += "ORDER BY log_id ASC;";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to list transaction logs: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        throw DatabaseException("Failed to store result: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    std::vector<TransactionLog> logs;
    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res)) != nullptr) {
        int logId = std::stoi(row[0]);
        int txId = std::stoi(row[1]);
        std::string op = row[2] ? row[2] : "";
        std::string msg = row[3] ? row[3] : "";
        std::string created = row[4] ? row[4] : "";
        logs.emplace_back(logId, txId, op, msg, created);
    }

    mysql_free_result(res);
    return logs;
}

// ----------------------------------------------------------------------------
// Transaction Control Primitives (ACID Foundations for Day 2)
// ----------------------------------------------------------------------------
void DatabaseManager::beginTransaction() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();
    if (mysql_query(conn_, "START TRANSACTION;") != 0) {
        throw DatabaseException("Failed to begin transaction: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }
}

void DatabaseManager::commit() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();
    if (mysql_query(conn_, "COMMIT;") != 0) {
        throw DatabaseException("Failed to commit transaction: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }
}

void DatabaseManager::rollback() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();
    if (mysql_query(conn_, "ROLLBACK;") != 0) {
        throw DatabaseException("Failed to rollback transaction: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }
}

void DatabaseManager::setAutoCommit(bool enable) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();
    std::string sql = enable ? "SET autocommit = 1;" : "SET autocommit = 0;";
    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to set autocommit: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }
}

// ----------------------------------------------------------------------------
// Task 4: Persistent Idempotency Operations
// ----------------------------------------------------------------------------

void DatabaseManager::ensureIdempotencyTableExists() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();
    const char* sql =
        "CREATE TABLE IF NOT EXISTS idempotency_records ("
        "    idempotency_key VARCHAR(128) PRIMARY KEY,"
        "    operation_type VARCHAR(32) NOT NULL,"
        "    request_hash VARCHAR(64) NOT NULL,"
        "    request_payload TEXT NOT NULL,"
        "    status ENUM('PROCESSING', 'COMMITTED', 'FAILED') NOT NULL DEFAULT 'PROCESSING',"
        "    response_code INT NOT NULL DEFAULT 0,"
        "    response_body MEDIUMTEXT NULL,"
        "    transaction_id INT NULL,"
        "    created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,"
        "    updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "    CONSTRAINT fk_idempotency_tx FOREIGN KEY (transaction_id) REFERENCES transactions(transaction_id) ON DELETE SET NULL,"
        "    INDEX idx_idempotency_created (created_at)"
        ") ENGINE=InnoDB;";

    if (mysql_query(conn_, sql) != 0) {
        throw DatabaseException("Failed to ensure idempotency_records table exists: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }
}

void DatabaseManager::cleanupOrphanedProcessingRecords() {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();
    const char* sql =
        "UPDATE idempotency_records SET status = 'FAILED', response_code = 500, "
        "response_body = '{\"error\":{\"code\":\"SERVER_RESTARTED\",\"message\":\"Transaction interrupted by server shutdown or restart\"},\"success\":false}', "
        "updated_at = NOW() WHERE status = 'PROCESSING';";
    mysql_query(conn_, sql);
}

bool DatabaseManager::createIdempotencyRecord(const IdempotencyRecord& record) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "INSERT INTO idempotency_records "
                      "(idempotency_key, operation_type, request_hash, request_payload, status, response_code, created_at) "
                      "VALUES ('" + escapeString(record.idempotencyKey) + "', '"
                                  + escapeString(record.operationType) + "', '"
                                  + escapeString(record.requestHash) + "', '"
                                  + escapeString(record.requestPayload) + "', '"
                                  + escapeString(record.status) + "', "
                                  + std::to_string(record.responseCode) + ", NOW());";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        unsigned int errNo = mysql_errno(conn_);
        if (errNo == 1062) { // ER_DUP_ENTRY / Duplicate primary key
            return false;
        }
        throw DatabaseException("Failed to create idempotency record: " + std::string(mysql_error(conn_)), errNo);
    }

    return mysql_affected_rows(conn_) > 0;
}

std::optional<IdempotencyRecord> DatabaseManager::getIdempotencyRecord(const std::string& key) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "SELECT idempotency_key, operation_type, request_hash, request_payload, "
                      "status, response_code, response_body, transaction_id, created_at, updated_at "
                      "FROM idempotency_records WHERE idempotency_key = '" + escapeString(key) + "' LIMIT 1;";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to fetch idempotency record: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_RES* res = mysql_store_result(conn_);
    if (!res) {
        throw DatabaseException("Failed to store result: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    std::optional<IdempotencyRecord> result = std::nullopt;
    if (row) {
        IdempotencyRecord rec;
        rec.idempotencyKey = row[0] ? row[0] : "";
        rec.operationType = row[1] ? row[1] : "";
        rec.requestHash = row[2] ? row[2] : "";
        rec.requestPayload = row[3] ? row[3] : "";
        rec.status = row[4] ? row[4] : "PROCESSING";
        rec.responseCode = row[5] ? std::stoi(row[5]) : 0;
        rec.responseBody = row[6] ? row[6] : "";
        if (row[7] != nullptr) {
            rec.transactionId = std::stoi(row[7]);
        } else {
            rec.transactionId = std::nullopt;
        }
        rec.createdAt = row[8] ? row[8] : "";
        rec.updatedAt = row[9] ? row[9] : "";
        result = rec;
    }

    mysql_free_result(res);
    return result;
}

bool DatabaseManager::commitIdempotencyRecord(const std::string& key, int responseCode, const std::string& responseBody, int transactionId) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string txIdStr = (transactionId > 0) ? std::to_string(transactionId) : "NULL";
    std::string sql = "UPDATE idempotency_records SET status = 'COMMITTED', response_code = " + std::to_string(responseCode)
                    + ", response_body = '" + escapeString(responseBody) + "', transaction_id = " + txIdStr
                    + ", updated_at = NOW() WHERE idempotency_key = '" + escapeString(key) + "';";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to commit idempotency record: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return mysql_affected_rows(conn_) > 0;
}

bool DatabaseManager::failIdempotencyRecord(const std::string& key, int responseCode, const std::string& responseBody) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "UPDATE idempotency_records SET status = 'FAILED', response_code = " + std::to_string(responseCode)
                    + ", response_body = '" + escapeString(responseBody) + "', updated_at = NOW() WHERE idempotency_key = '"
                    + escapeString(key) + "';";

    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to mark idempotency record failed: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return mysql_affected_rows(conn_) > 0;
}

bool DatabaseManager::deleteIdempotencyRecord(const std::string& key) {
    std::lock_guard<std::mutex> lock(dbMutex_);
    ensureConnected();

    std::string sql = "DELETE FROM idempotency_records WHERE idempotency_key = '" + escapeString(key) + "';";
    if (mysql_query(conn_, sql.c_str()) != 0) {
        throw DatabaseException("Failed to delete idempotency record: " + std::string(mysql_error(conn_)), mysql_errno(conn_));
    }

    return mysql_affected_rows(conn_) > 0;
}

} // namespace paylock
