#ifndef PAYLOCK_USER_H
#define PAYLOCK_USER_H

#include <string>

namespace paylock {

/**
 * @brief Represents a user entity in the PayLock system.
 */
struct User {
    int userId{0};
    std::string name;
    std::string email;
    std::string createdAt;

    User() = default;
    User(int id, std::string n, std::string e, std::string created = "")
        : userId(id), name(std::move(n)), email(std::move(e)), createdAt(std::move(created)) {}
};

} // namespace paylock

#endif // PAYLOCK_USER_H
