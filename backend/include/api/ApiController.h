#ifndef PAYLOCK_API_CONTROLLER_H
#define PAYLOCK_API_CONTROLLER_H

#include <httplib.h>
#include <nlohmann/json.hpp>
#include "database/DatabaseManager.h"
#include "transaction/TransactionManager.h"
#include "locking/LockManager.h"
#include "deadlock/DeadlockDetector.h"
#include "recovery/RecoveryManager.h"
#include "concurrency/ConcurrencyManager.h"
#include "resources/ResourceManager.h"
#include "resources/PagingSimulator.h"

namespace paylock {
namespace api {

/**
 * @brief Coordinates HTTP request routing, input validation, serialization,
 * and calls into underlying PayLock core services.
 */
class ApiController {
public:
    ApiController(DatabaseConfig dbConfig,
                  DatabaseManager& adminDb,
                  LockManager& lockManager,
                  RecoveryManager& recoveryManager,
                  ResourceManager& resourceManager,
                  PagingSimulator& pagingSimulator,
                  ConcurrencyManager* concurrencyManager = nullptr);

    /**
     * @brief Registers all /api/v1 endpoints with the HTTP server.
     */
    void registerRoutes(httplib::Server& server);

    // --- Route Handlers ---

    // A. Health & System
    void handleHealth(const httplib::Request& req, httplib::Response& res);
    void handleSystemStats(const httplib::Request& req, httplib::Response& res);

    // B. Accounts
    void handleListAccounts(const httplib::Request& req, httplib::Response& res);
    void handleGetAccount(const httplib::Request& req, httplib::Response& res);

    // C. Transactions
    void handleListTransactions(const httplib::Request& req, httplib::Response& res);
    void handleGetTransaction(const httplib::Request& req, httplib::Response& res);
    void handleTransfer(const httplib::Request& req, httplib::Response& res);
    void handleDeposit(const httplib::Request& req, httplib::Response& res);
    void handleWithdraw(const httplib::Request& req, httplib::Response& res);

    // D. Concurrency
    void handleConcurrencyStatus(const httplib::Request& req, httplib::Response& res);
    void handleConcurrencyTransactions(const httplib::Request& req, httplib::Response& res);

    // E. Locks
    void handleLocks(const httplib::Request& req, httplib::Response& res);
    void handleWaitForGraph(const httplib::Request& req, httplib::Response& res);

    // F. Deadlocks & Recovery
    void handleDeadlocks(const httplib::Request& req, httplib::Response& res);
    void handleRecoveryHistory(const httplib::Request& req, httplib::Response& res);

    // G. Resources & Paging
    void handleResources(const httplib::Request& req, httplib::Response& res);
    void handlePagingStats(const httplib::Request& req, httplib::Response& res);
    void handlePagingFrames(const httplib::Request& req, httplib::Response& res);

private:
    DatabaseConfig dbConfig_;
    DatabaseManager& adminDb_;
    LockManager& lockManager_;
    RecoveryManager& recoveryManager_;
    ResourceManager& resourceManager_;
    PagingSimulator& pagingSimulator_;
    ConcurrencyManager* concurrencyManager_{nullptr};

    // Helper to parse integer account ID from string or int in JSON
    static std::optional<int> parseAccountId(const nlohmann::json& j, const std::string& key);
};

} // namespace api
} // namespace paylock

#endif // PAYLOCK_API_CONTROLLER_H
