#ifndef PAYLOCK_ACCOUNT_H
#define PAYLOCK_ACCOUNT_H

#include <string>

namespace paylock {

/**
 * @brief Represents a bank/wallet account in PayLock.
 * 
 * DBMS Concept: Consistency & Integrity.
 * Account balance must be strictly non-negative (enforced by DB check constraint).
 */
struct Account {
    int accountId{0};
    int userId{0};
    std::string ownerName; // Joined from users table for display convenience
    double balance{0.0};
    std::string createdAt;

    Account() = default;
    Account(int accId, int uId, std::string name, double bal, std::string created = "")
        : accountId(accId), userId(uId), ownerName(std::move(name)), balance(bal), createdAt(std::move(created)) {}
};

} // namespace paylock

#endif // PAYLOCK_ACCOUNT_H
