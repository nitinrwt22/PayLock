#ifndef PAYLOCK_TRANSACTION_LOG_H
#define PAYLOCK_TRANSACTION_LOG_H

#include <string>

namespace paylock {

/**
 * @brief Represents an audit/WAL log entry for a transaction.
 * 
 * DBMS Concept: Durability & Write-Ahead Logging (WAL).
 */
struct TransactionLog {
    int logId{0};
    int transactionId{0};
    std::string operation;
    std::string message;
    std::string createdAt;

    TransactionLog() = default;
    TransactionLog(int lId, int txId, std::string op, std::string msg, std::string created = "")
        : logId(lId), transactionId(txId), operation(std::move(op)), message(std::move(msg)), createdAt(std::move(created)) {}
};

} // namespace paylock

#endif // PAYLOCK_TRANSACTION_LOG_H
