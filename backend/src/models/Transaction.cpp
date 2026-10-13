#if __has_include("models/Transaction.h")
#include "models/Transaction.h"
#else
#include "../../include/models/Transaction.h"
#endif

#include <string>
#include <algorithm>

namespace paylock {

std::string statusToString(TransactionStatus status) {
    switch (status) {
        case TransactionStatus::PENDING:     return "PENDING";
        case TransactionStatus::ACTIVE:      return "ACTIVE";
        case TransactionStatus::PROCESSING:  return "PROCESSING";
        case TransactionStatus::COMMITTED:   return "COMMITTED";
        case TransactionStatus::ROLLED_BACK: return "ROLLED_BACK";
        case TransactionStatus::FAILED:      return "FAILED";
        default:                             return "UNKNOWN";
    }
}

TransactionStatus stringToStatus(const std::string& str) {
    if (str == "PENDING")     return TransactionStatus::PENDING;
    if (str == "ACTIVE")      return TransactionStatus::ACTIVE;
    if (str == "PROCESSING")  return TransactionStatus::PROCESSING;
    if (str == "COMMITTED")   return TransactionStatus::COMMITTED;
    if (str == "ROLLED_BACK") return TransactionStatus::ROLLED_BACK;
    if (str == "FAILED")      return TransactionStatus::FAILED;
    return TransactionStatus::PENDING;
}

bool isValidStatusTransition(TransactionStatus current, TransactionStatus target) {
    if (current == target) return true;

    switch (current) {
        case TransactionStatus::PENDING:
            return target == TransactionStatus::ACTIVE || 
                   target == TransactionStatus::PROCESSING ||
                   target == TransactionStatus::FAILED ||
                   target == TransactionStatus::ROLLED_BACK;
        case TransactionStatus::ACTIVE:
        case TransactionStatus::PROCESSING:
            return target == TransactionStatus::COMMITTED ||
                   target == TransactionStatus::ROLLED_BACK ||
                   target == TransactionStatus::FAILED;
        case TransactionStatus::COMMITTED:
        case TransactionStatus::ROLLED_BACK:
        case TransactionStatus::FAILED:
            // Terminal states: no transitions out of terminal states
            return false;
        default:
            return false;
    }
}

} // namespace paylock
