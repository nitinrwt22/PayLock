#ifndef PAYLOCK_IDEMPOTENCY_RECORD_H
#define PAYLOCK_IDEMPOTENCY_RECORD_H

#include <string>
#include <optional>
#include <iostream>

namespace paylock {

/**
 * @brief Represents an idempotency record stored in MySQL to guarantee that
 * mutating payment operations (transfers, deposits, withdrawals) are executed exactly once.
 */
struct IdempotencyRecord {
    std::string idempotencyKey;
    std::string operationType;      // "TRANSFER", "DEPOSIT", "WITHDRAW"
    std::string requestHash;        // SHA-256 hex string of canonical request parameters
    std::string requestPayload;     // Canonical representation of request parameters
    std::string status{"PROCESSING"}; // "PROCESSING", "COMMITTED", "FAILED"
    int responseCode{0};
    std::string responseBody;
    std::optional<int> transactionId{std::nullopt};
    std::string createdAt;
    std::string updatedAt;

    IdempotencyRecord() = default;

    IdempotencyRecord(std::string key,
                      std::string opType,
                      std::string reqHash,
                      std::string reqPayload,
                      std::string st = "PROCESSING",
                      int respCode = 0,
                      std::string respBody = "",
                      std::optional<int> txId = std::nullopt)
        : idempotencyKey(std::move(key)),
          operationType(std::move(opType)),
          requestHash(std::move(reqHash)),
          requestPayload(std::move(reqPayload)),
          status(std::move(st)),
          responseCode(respCode),
          responseBody(std::move(respBody)),
          transactionId(txId) {}

    bool isProcessing() const noexcept { return status == "PROCESSING"; }
    bool isCommitted() const noexcept { return status == "COMMITTED"; }
    bool isFailed() const noexcept { return status == "FAILED"; }
};

} // namespace paylock

#endif // PAYLOCK_IDEMPOTENCY_RECORD_H
