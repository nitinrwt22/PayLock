#ifndef PAYLOCK_TRANSACTION_H
#define PAYLOCK_TRANSACTION_H

#include <string>

namespace paylock {

/**
 * @brief Represents a financial transaction record.
 * 
 * DBMS Concept: Atomicity & State Lifecycle.
 * Possible states: PENDING, PROCESSING, COMMITTED, ROLLED_BACK, FAILED
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

    bool isDeposit() const noexcept { return senderAccount == 0 && receiverAccount > 0; }
    bool isWithdrawal() const noexcept { return senderAccount > 0 && receiverAccount == 0; }
    bool isTransfer() const noexcept { return senderAccount > 0 && receiverAccount > 0; }
};

} // namespace paylock

#endif // PAYLOCK_TRANSACTION_H
