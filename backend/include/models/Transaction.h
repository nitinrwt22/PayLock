#ifndef PAYLOCK_TRANSACTION_H
#define PAYLOCK_TRANSACTION_H

#include <string>
#include <iostream>

namespace paylock {

/**
 * @brief Transaction lifecycle states.
 * 
 * Formal transitions:
 * - PENDING -> ACTIVE (or PROCESSING) -> COMMITTED
 * - PENDING -> ACTIVE (or PROCESSING) -> ROLLED_BACK
 * - PENDING -> ACTIVE (or PROCESSING) -> FAILED
 */
enum class TransactionStatus {
    PENDING,
    ACTIVE,
    PROCESSING,
    COMMITTED,
    ROLLED_BACK,
    FAILED
};

std::string statusToString(TransactionStatus status);
TransactionStatus stringToStatus(const std::string& str);
bool isValidStatusTransition(TransactionStatus current, TransactionStatus target);

/**
 * @brief Represents a financial transaction entity in PayLock.
 * 
 * Tracks sender, receiver, exact monetary amount, lifecycle status, and timestamp.
 * Special account IDs:
 * - senderAccount = 0: External deposit (Cash/System source)
 * - receiverAccount = 0: External withdrawal (Cash/System sink)
 */
struct Transaction {
    int transactionId{0};
    int senderAccount{0};
    int receiverAccount{0};
    double amount{0.0};
    std::string status{"PENDING"};
    std::string createdAt;

    Transaction() = default;
    Transaction(int txId, int sender, int receiver, double amt, std::string st = "PENDING", std::string created = "")
        : transactionId(txId), senderAccount(sender), receiverAccount(receiver),
          amount(amt), status(std::move(st)), createdAt(std::move(created)) {}

    Transaction(int txId, int sender, int receiver, double amt, TransactionStatus st, std::string created = "")
        : transactionId(txId), senderAccount(sender), receiverAccount(receiver),
          amount(amt), status(statusToString(st)), createdAt(std::move(created)) {}

    TransactionStatus getStatusEnum() const {
        return stringToStatus(status);
    }

    void setStatus(TransactionStatus st) {
        status = statusToString(st);
    }

    void setStatus(const std::string& st) {
        status = st;
    }

    bool isDeposit() const noexcept { return senderAccount == 0 && receiverAccount > 0; }
    bool isWithdrawal() const noexcept { return senderAccount > 0 && receiverAccount == 0; }
    bool isTransfer() const noexcept { return senderAccount > 0 && receiverAccount > 0; }

    bool isPending() const noexcept { return status == "PENDING"; }
    bool isActive() const noexcept { return status == "ACTIVE" || status == "PROCESSING"; }
    bool isCommitted() const noexcept { return status == "COMMITTED"; }
    bool isRolledBack() const noexcept { return status == "ROLLED_BACK"; }
    bool isFailed() const noexcept { return status == "FAILED"; }
};

} // namespace paylock

#endif // PAYLOCK_TRANSACTION_H
