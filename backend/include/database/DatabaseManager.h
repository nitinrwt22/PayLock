#ifndef PAYLOCK_DATABASE_MANAGER_H
#define PAYLOCK_DATABASE_MANAGER_H

#if __has_include(<mysql.h>)
#include <mysql.h>
#elif __has_include(<mysql/mysql.h>)
#include <mysql/mysql.h>
#else
#include <mysql.h>
#endif
#include <string>
#include <vector>
#include <optional>
#include <mutex>
#include <stdexcept>
#include "models/User.h"
#include "models/Account.h"
#include "models/Transaction.h"
#include "models/TransactionLog.h"
#include "models/IdempotencyRecord.h"

namespace paylock {

/**
 * @brief Custom exception class for database operations.
 * Captures MySQL error codes and descriptive error text.
 */
class DatabaseException : public std::runtime_error {
public:
    DatabaseException(const std::string& message, unsigned int errorCode = 0)
        : std::runtime_error(message + (errorCode ? " [MySQL Error " + std::to_string(errorCode) + "]" : "")),
          errorCode_(errorCode) {}

    unsigned int getErrorCode() const noexcept { return errorCode_; }

private:
    unsigned int errorCode_;
};

/**
 * @brief Configuration parameters for MySQL database connectivity.
 */
struct DatabaseConfig {
    std::string host{"127.0.0.1"};
    std::string user{"root"};
    std::string password{"nitin"};
    std::string database{"paylock_db"};
    unsigned int port{3306};
    std::string socketPath{""};

    static DatabaseConfig fromEnvOrDefault();
};

/**
 * @brief DatabaseManager handles MySQL connectivity, query execution,
 * transactions, and data modeling for PayLock.
 * 
 * OS Concepts Demonstrated:
 * - Mutex synchronization (internal mutex guards queries for future thread safety)
 * - Safe resource reclamation (RAII for MySQL connection handles)
 * 
 * DBMS Concepts Demonstrated:
 * - Connection lifecycle management
 * - Integrity constraints and prepared statements to prevent injection
 * - Foundations for ACID transactions (START TRANSACTION, COMMIT, ROLLBACK)
 */
class DatabaseManager {
public:
    DatabaseManager();
    explicit DatabaseManager(DatabaseConfig config);
    ~DatabaseManager();

    // Prevent copy semantics to avoid double-free of MYSQL connection
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    // Allow move semantics
    DatabaseManager(DatabaseManager&& other) noexcept;
    DatabaseManager& operator=(DatabaseManager&& other) noexcept;

    // Connection Lifecycle
    void connect();
    void connect(const DatabaseConfig& config);
    void disconnect() noexcept;
    bool isConnected() noexcept;
    MYSQL* getRawConnection() noexcept;

    // Day 1 Operations:
    int createUser(const std::string& name, const std::string& email);
    bool createAccount(int accountId, int userId, double initialBalance);
    std::optional<double> getAccountBalance(int accountId);
    std::optional<Account> getAccountById(int accountId);
    bool accountExists(int accountId);
    std::vector<Account> listAccounts();
    std::vector<Transaction> listTransactions();
    int recordTransaction(int senderAccount, int receiverAccount, double amount, const std::string& status = "PENDING");
    bool recordTransactionLog(int transactionId, const std::string& operation, const std::string& message);

    // Day 2 Account Balance Operations:
    bool debitAccount(int accountId, double amount);
    bool creditAccount(int accountId, double amount);

    // Day 2 Transaction Query & Lifecycle Operations:
    bool updateTransactionStatus(int transactionId, const std::string& status);
    std::optional<Transaction> getTransactionById(int transactionId);
    std::vector<Transaction> getTransactionsForAccount(int accountId);
    std::vector<TransactionLog> listTransactionLogs(int transactionId = 0);

    // Day 2 Transaction Control Primitives (ACID Foundations)
    void beginTransaction();
    void commit();
    void rollback();
    void setAutoCommit(bool enable);

    // Task 4: Persistent Idempotency Operations
    void ensureIdempotencyTableExists();
    void cleanupOrphanedProcessingRecords();
    bool createIdempotencyRecord(const IdempotencyRecord& record);
    std::optional<IdempotencyRecord> getIdempotencyRecord(const std::string& key);
    bool commitIdempotencyRecord(const std::string& key, int responseCode, const std::string& responseBody, int transactionId);
    bool failIdempotencyRecord(const std::string& key, int responseCode, const std::string& responseBody);
    bool deleteIdempotencyRecord(const std::string& key);

    // Raw Query Utilities
    void executeQuery(const std::string& sql);

private:
    void ensureConnected();
    std::string escapeString(const std::string& str);

    DatabaseConfig config_;
    MYSQL* conn_{nullptr};
    mutable std::mutex dbMutex_; // Mutex synchronization for thread-safe access
};

} // namespace paylock

#endif // PAYLOCK_DATABASE_MANAGER_H
