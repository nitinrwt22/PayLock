#ifndef PAYLOCK_IDEMPOTENCY_MANAGER_H
#define PAYLOCK_IDEMPOTENCY_MANAGER_H

#include "models/IdempotencyRecord.h"
#include "database/DatabaseManager.h"
#include <string>
#include <optional>
#include <chrono>
#include <thread>

namespace paylock {

/**
 * @brief Result of claiming or checking an idempotency key.
 */
enum class IdempotencyClaimResult {
    CLAIMED,        ///< Brand new key successfully claimed; proceed with operation
    REPLAY_SUCCESS, ///< Key was previously committed; return stored successful response
    REPLAY_FAILED,  ///< Key was previously failed; return stored failure response
    CONFLICT,       ///< Key was previously used with a different operation or payload (HTTP 409)
    IN_FLIGHT,      ///< Request with this key is currently being processed by a concurrent thread
    DATABASE_ERROR  ///< Database connectivity or query error
};

/**
 * @brief Coordinates idempotency verification, request fingerprinting,
 * persistence, and safe concurrent replay for PayLock payment operations.
 */
class IdempotencyManager {
public:
    /**
     * @brief Validates that an Idempotency-Key header is well-formed.
     * Must be a non-empty ASCII string of 1 to 128 printable characters without whitespace.
     */
    static bool isValidKey(const std::string& key) noexcept;

    /**
     * @brief Computes SHA-256 hex digest for a string using OpenSSL.
     */
    static std::string computeSha256(const std::string& input);

    /**
     * @brief Formats a monetary amount to 2 decimal places.
     */
    static std::string formatAmount(double amount);

    /**
     * @brief Builds canonical payload strings for fingerprinting payment requests.
     */
    static std::string buildTransferPayload(int fromAccount, int toAccount, double amount);
    static std::string buildDepositPayload(int accountId, double amount);
    static std::string buildWithdrawPayload(int accountId, double amount);

    /**
     * @brief Attempts to atomically claim an idempotency key or retrieve its existing record.
     * If the key is currently PROCESSING by another thread, polls up to maxWaitMs.
     * 
     * @param db DatabaseManager connection handle
     * @param key Client-provided idempotency key
     * @param operation "TRANSFER", "DEPOSIT", or "WITHDRAW"
     * @param requestHash SHA-256 digest of canonical request payload
     * @param canonicalPayload Canonical string representation of request parameters
     * @param outRecord Populated with existing record if already present
     * @param maxWaitMs Maximum milliseconds to wait if key is currently IN_FLIGHT (default: 2500ms)
     * @return IdempotencyClaimResult
     */
    static IdempotencyClaimResult claimOrCheck(DatabaseManager& db,
                                              const std::string& key,
                                              const std::string& operation,
                                              const std::string& requestHash,
                                              const std::string& canonicalPayload,
                                              IdempotencyRecord& outRecord,
                                              int maxWaitMs = 2500);

    /**
     * @brief Finalizes an idempotency record upon successful commit of financial transaction.
     */
    static bool recordCommitted(DatabaseManager& db,
                               const std::string& key,
                               int responseCode,
                               const std::string& responseBody,
                               int transactionId);

    /**
     * @brief Finalizes an idempotency record upon rollback / failure of financial transaction.
     */
    static bool recordFailed(DatabaseManager& db,
                            const std::string& key,
                            int responseCode,
                            const std::string& responseBody);
};

} // namespace paylock

#endif // PAYLOCK_IDEMPOTENCY_MANAGER_H
