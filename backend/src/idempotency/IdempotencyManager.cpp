#include "idempotency/IdempotencyManager.h"
#include "logging/Logger.h"
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <cctype>

namespace paylock {

bool IdempotencyManager::isValidKey(const std::string& key) noexcept {
    if (key.empty() || key.length() > 128) {
        return false;
    }
    // Must be printable ASCII characters without space or control characters
    for (unsigned char ch : key) {
        if (ch <= 32 || ch >= 127) {
            return false;
        }
    }
    return true;
}

std::string IdempotencyManager::computeSha256(const std::string& input) {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(input.data()), input.size(), hash);
    std::ostringstream hexStream;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        hexStream << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    return hexStream.str();
}

std::string IdempotencyManager::formatAmount(double amount) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(2) << amount;
    return ss.str();
}

std::string IdempotencyManager::buildTransferPayload(int fromAccount, int toAccount, double amount) {
    return "TRANSFER:from=" + std::to_string(fromAccount)
         + ":to=" + std::to_string(toAccount)
         + ":amount=" + formatAmount(amount);
}

std::string IdempotencyManager::buildDepositPayload(int accountId, double amount) {
    return "DEPOSIT:account=" + std::to_string(accountId)
         + ":amount=" + formatAmount(amount);
}

std::string IdempotencyManager::buildWithdrawPayload(int accountId, double amount) {
    return "WITHDRAW:account=" + std::to_string(accountId)
         + ":amount=" + formatAmount(amount);
}

IdempotencyClaimResult IdempotencyManager::claimOrCheck(DatabaseManager& db,
                                                      const std::string& key,
                                                      const std::string& operation,
                                                      const std::string& requestHash,
                                                      const std::string& canonicalPayload,
                                                      IdempotencyRecord& outRecord,
                                                      int maxWaitMs) {
    IdempotencyRecord newRecord(key, operation, requestHash, canonicalPayload, "PROCESSING", 0, "", std::nullopt);

    // 1. Attempt initial claim via INSERT
    bool inserted = false;
    try {
        inserted = db.createIdempotencyRecord(newRecord);
    } catch (const DatabaseException& ex) {
        Logger::error("Database error claiming idempotency key '" + key + "': " + ex.what(), "IdempotencyManager");
        return IdempotencyClaimResult::DATABASE_ERROR;
    }

    if (inserted) {
        outRecord = newRecord;
        Logger::info("Idempotency key claimed for operation " + operation + " [Key: " + key + "]", "IdempotencyManager");
        return IdempotencyClaimResult::CLAIMED;
    }

    // 2. Key already exists in database; fetch existing record
    std::optional<IdempotencyRecord> existingOpt;
    try {
        existingOpt = db.getIdempotencyRecord(key);
    } catch (const DatabaseException& ex) {
        Logger::error("Failed to query existing idempotency record: " + std::string(ex.what()), "IdempotencyManager");
        return IdempotencyClaimResult::DATABASE_ERROR;
    }

    if (!existingOpt.has_value()) {
        // Race condition: row was deleted or uncommitted rollback
        return IdempotencyClaimResult::DATABASE_ERROR;
    }

    auto existing = existingOpt.value();

    // 3. Payload / Operation Conflict Validation
    if (existing.operationType != operation || existing.requestHash != requestHash) {
        Logger::warn("Idempotency conflict detected for key '" + key + "'. Existing op=" + existing.operationType
                    + " vs req op=" + operation, "IdempotencyManager");
        outRecord = existing;
        return IdempotencyClaimResult::CONFLICT;
    }

    // 4. Completed state: return stored result immediately
    if (existing.status == "COMMITTED") {
        Logger::info("Replaying COMMITTED outcome for idempotency key '" + key + "'", "IdempotencyManager");
        outRecord = existing;
        return IdempotencyClaimResult::REPLAY_SUCCESS;
    }

    // 5. Failed state: return stored failure outcome
    if (existing.status == "FAILED") {
        Logger::info("Replaying FAILED outcome for idempotency key '" + key + "'", "IdempotencyManager");
        outRecord = existing;
        return IdempotencyClaimResult::REPLAY_FAILED;
    }

    // 6. PROCESSING state: Concurrent request in flight
    // Wait for the in-flight request to finish, polling every 25ms up to maxWaitMs
    const int pollIntervalMs = 25;
    int elapsed = 0;
    while (elapsed < maxWaitMs) {
        std::this_thread::sleep_for(std::chrono::milliseconds(pollIntervalMs));
        elapsed += pollIntervalMs;

        try {
            auto pollOpt = db.getIdempotencyRecord(key);
            if (!pollOpt.has_value()) {
                // Key was deleted or released by aborted request; attempt re-claim
                if (db.createIdempotencyRecord(newRecord)) {
                    outRecord = newRecord;
                    Logger::info("Idempotency key re-claimed for operation " + operation + " [Key: " + key + "]", "IdempotencyManager");
                    return IdempotencyClaimResult::CLAIMED;
                }
            } else {
                auto current = pollOpt.value();
                if (current.status == "COMMITTED") {
                    Logger::info("Concurrent in-flight request for key '" + key + "' committed; replaying outcome", "IdempotencyManager");
                    outRecord = current;
                    return IdempotencyClaimResult::REPLAY_SUCCESS;
                }
                if (current.status == "FAILED") {
                    Logger::info("Concurrent in-flight request for key '" + key + "' failed; replaying outcome", "IdempotencyManager");
                    outRecord = current;
                    return IdempotencyClaimResult::REPLAY_FAILED;
                }
            }
        } catch (...) {
            // Ignore transient query errors during polling
        }
    }

    Logger::warn("Timed out waiting for concurrent in-flight request for key '" + key + "'", "IdempotencyManager");
    outRecord = existing;
    return IdempotencyClaimResult::IN_FLIGHT;
}

bool IdempotencyManager::recordCommitted(DatabaseManager& db,
                                        const std::string& key,
                                        int responseCode,
                                        const std::string& responseBody,
                                        int transactionId) {
    try {
        return db.commitIdempotencyRecord(key, responseCode, responseBody, transactionId);
    } catch (const std::exception& ex) {
        Logger::error("Failed to mark idempotency record committed: " + std::string(ex.what()), "IdempotencyManager");
        return false;
    }
}

bool IdempotencyManager::recordFailed(DatabaseManager& db,
                                     const std::string& key,
                                     int responseCode,
                                     const std::string& responseBody) {
    try {
        return db.failIdempotencyRecord(key, responseCode, responseBody);
    } catch (const std::exception& ex) {
        Logger::error("Failed to mark idempotency record failed: " + std::string(ex.what()), "IdempotencyManager");
        return false;
    }
}

} // namespace paylock
