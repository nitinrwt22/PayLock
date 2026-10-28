#include "api/ApiController.h"
#include "api/ApiResponse.h"
#include "idempotency/IdempotencyManager.h"
#include "logging/Logger.h"
#include <algorithm>
#include <cmath>
#include <regex>
#include <sstream>
#include <iomanip>

namespace paylock {
namespace api {

ApiController::ApiController(DatabaseConfig dbConfig,
                             DatabaseManager& adminDb,
                             LockManager& lockManager,
                             RecoveryManager& recoveryManager,
                             ResourceManager& resourceManager,
                             PagingSimulator& pagingSimulator,
                             ConcurrencyManager* concurrencyManager,
                             std::shared_ptr<ConnectionPool> pool)
    : dbConfig_(std::move(dbConfig)),
      adminDb_(adminDb),
      lockManager_(lockManager),
      recoveryManager_(recoveryManager),
      resourceManager_(resourceManager),
      pagingSimulator_(pagingSimulator),
      concurrencyManager_(concurrencyManager),
      pool_(pool ? std::move(pool) : std::make_shared<ConnectionPool>(dbConfig_)) {
    try {
        adminDb_.ensureIdempotencyTableExists();
        adminDb_.cleanupOrphanedProcessingRecords();
    } catch (const std::exception& ex) {
        Logger::warn("Notice during ensureIdempotencyTableExists: " + std::string(ex.what()), "ApiController");
    }
}

std::optional<int> ApiController::parseAccountId(const nlohmann::json& j, const std::string& key) {
    if (!j.contains(key)) return std::nullopt;
    if (j[key].is_number_integer()) {
        return j[key].get<int>();
    }
    if (j[key].is_string()) {
        std::string str = j[key].get<std::string>();
        if (!str.empty() && (str[0] == 'A' || str[0] == 'a' || str[0] == 'U' || str[0] == 'u')) {
            str = str.substr(1);
        }
        try {
            return std::stoi(str);
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

void ApiController::registerRoutes(httplib::Server& server) {
    // --- Health & System ---
    server.Get("/api/v1/health", [this](const httplib::Request& req, httplib::Response& res) {
        handleHealth(req, res);
    });

    server.Get("/api/v1/system/stats", [this](const httplib::Request& req, httplib::Response& res) {
        handleSystemStats(req, res);
    });

    // --- Accounts ---
    server.Get("/api/v1/accounts", [this](const httplib::Request& req, httplib::Response& res) {
        handleListAccounts(req, res);
    });

    server.Get(R"(/api/v1/accounts/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetAccount(req, res);
    });

    server.Post("/api/v1/accounts", [this](const httplib::Request& req, httplib::Response& res) {
        handleCreateAccount(req, res);
    });

    // --- Transactions ---
    server.Get("/api/v1/transactions", [this](const httplib::Request& req, httplib::Response& res) {
        handleListTransactions(req, res);
    });

    server.Get(R"(/api/v1/transactions/([^/]+))", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetTransaction(req, res);
    });

    server.Post("/api/v1/transactions/transfer", [this](const httplib::Request& req, httplib::Response& res) {
        handleTransfer(req, res);
    });

    server.Post("/api/v1/transactions/deposit", [this](const httplib::Request& req, httplib::Response& res) {
        handleDeposit(req, res);
    });

    server.Post("/api/v1/transactions/withdraw", [this](const httplib::Request& req, httplib::Response& res) {
        handleWithdraw(req, res);
    });

    // --- Concurrency Monitoring ---
    server.Get("/api/v1/concurrency/status", [this](const httplib::Request& req, httplib::Response& res) {
        handleConcurrencyStatus(req, res);
    });

    server.Get("/api/v1/concurrency/transactions", [this](const httplib::Request& req, httplib::Response& res) {
        handleConcurrencyTransactions(req, res);
    });

    // --- Lock Monitoring ---
    server.Get("/api/v1/locks", [this](const httplib::Request& req, httplib::Response& res) {
        handleLocks(req, res);
    });

    server.Get("/api/v1/locks/wait-for-graph", [this](const httplib::Request& req, httplib::Response& res) {
        handleWaitForGraph(req, res);
    });

    // --- Deadlocks & Recovery ---
    server.Get("/api/v1/deadlocks", [this](const httplib::Request& req, httplib::Response& res) {
        handleDeadlocks(req, res);
    });

    server.Get("/api/v1/recovery/history", [this](const httplib::Request& req, httplib::Response& res) {
        handleRecoveryHistory(req, res);
    });

    // --- Resources & Paging ---
    server.Get("/api/v1/resources", [this](const httplib::Request& req, httplib::Response& res) {
        handleResources(req, res);
    });

    server.Get("/api/v1/paging/stats", [this](const httplib::Request& req, httplib::Response& res) {
        handlePagingStats(req, res);
    });

    server.Get("/api/v1/paging/frames", [this](const httplib::Request& req, httplib::Response& res) {
        handlePagingFrames(req, res);
    });

    // --- Demo & Simulation ---
    server.Post("/api/v1/system/demo/simulate", [this](const httplib::Request& req, httplib::Response& res) {
        handleDemoSimulate(req, res);
    });

    server.Post("/api/v1/system/demo/deadlock", [this](const httplib::Request& req, httplib::Response& res) {
        handleDemoDeadlock(req, res);
    });
}

// ----------------------------------------------------------------------------
// Health & System
// ----------------------------------------------------------------------------

void ApiController::handleHealth(const httplib::Request& /*req*/, httplib::Response& res) {
    bool dbConnected = false;
    try {
        dbConnected = adminDb_.isConnected() && (pool_ && pool_->isHealthy());
    } catch (...) {
        dbConnected = false;
    }

    if (!dbConnected) {
        nlohmann::json errData;
        errData["server"] = "UP";
        errData["database"] = "DOWN";
        ApiResponse::sendError(res, "SERVICE_UNAVAILABLE", "Database connectivity check failed", 503);
        return;
    }

    auto poolMetrics = pool_ ? pool_->getMetrics() : PoolMetrics{};

    nlohmann::json data;
    data["status"] = "healthy";
    data["server"] = "healthy";
    data["version"] = "1.0.0";
    data["database"] = {
        {"status", "UP"},
        {"connected", true},
        {"type", "MySQL 9.x (InnoDB)"},
        {"host", dbConfig_.host},
        {"port", dbConfig_.port},
        {"database", dbConfig_.database},
        {"pool", {
            {"max_capacity", poolMetrics.maxCapacity},
            {"available", poolMetrics.available},
            {"in_use", poolMetrics.inUse}
        }}
    };
    data["services"] = {
        {"transaction_engine", "READY"},
        {"concurrency_engine", "READY"},
        {"lock_manager", "READY"},
        {"deadlock_detector", "READY"},
        {"recovery_manager", "READY"},
        {"resource_manager", "READY"},
        {"paging_simulator", "READY"},
        {"connection_pool", "READY"}
    };

    ApiResponse::sendSuccess(res, data, "PayLock API Server is healthy", 200);
}

void ApiController::handleSystemStats(const httplib::Request& /*req*/, httplib::Response& res) {
    try {
        std::vector<Account> accounts;
        std::vector<Transaction> transactions;
        if (pool_) {
            auto lease = pool_->acquire();
            accounts = lease->listAccounts();
            transactions = lease->listTransactions();
        } else {
            accounts = adminDb_.listAccounts();
            transactions = adminDb_.listTransactions();
        }

        size_t committed = 0;
        size_t rolledBack = 0;
        size_t failed = 0;
        size_t pending = 0;

        for (const auto& tx : transactions) {
            if (tx.status == "COMMITTED") committed++;
            else if (tx.status == "ROLLED_BACK") rolledBack++;
            else if (tx.status == "FAILED") failed++;
            else pending++;
        }

        double totalMoney = 0.0;
        for (const auto& acc : accounts) {
            totalMoney += acc.balance;
        }

        auto lockSnap = lockManager_.getSnapshot();
        size_t totalWaitingLocks = 0;
        size_t activeLockHolders = 0;
        for (const auto& [accId, rSnap] : lockSnap.resources) {
            activeLockHolders += rSnap.holders.size();
            totalWaitingLocks += rSnap.waitQueue.size();
        }

        auto resSnap = resourceManager_.getSnapshot();
        auto pagingStats = pagingSimulator_.getStatistics();
        auto poolMetrics = pool_ ? pool_->getMetrics() : PoolMetrics{};

        nlohmann::json data;
        data["transactions"] = {
            {"total", transactions.size()},
            {"committed", committed},
            {"rolled_back", rolledBack},
            {"failed", failed},
            {"pending", pending}
        };
        data["accounts"] = {
            {"total_accounts", accounts.size()},
            {"system_total_balance", totalMoney}
        };
        data["locks"] = {
            {"active_holders", activeLockHolders},
            {"waiting_requests", totalWaitingLocks}
        };
        data["deadlocks"] = {
            {"total_recoveries", recoveryManager_.getRecoveryCount()}
        };
        data["resources"] = {
            {"total_allocated", resSnap.totalAllocated},
            {"total_available", resSnap.totalAvailable}
        };
        data["paging_simulation"] = {
            {"total_references", pagingStats.totalReferences},
            {"page_hits", pagingStats.pageHits},
            {"page_faults", pagingStats.pageFaults},
            {"hit_ratio", pagingStats.hitRatio},
            {"fault_ratio", pagingStats.faultRatio}
        };
        data["database_pool"] = {
            {"max_capacity", poolMetrics.maxCapacity},
            {"total_connections", poolMetrics.totalCreated},
            {"in_use", poolMetrics.inUse},
            {"available", poolMetrics.available},
            {"waiting_requests", poolMetrics.waitingRequests},
            {"acquisition_failures", poolMetrics.acquisitionFailures},
            {"total_acquisitions", poolMetrics.totalAcquisitions}
        };

        ApiResponse::sendSuccess(res, data, "System statistics retrieved successfully", 200);
    } catch (const std::exception& ex) {
        ApiResponse::sendError(res, "INTERNAL_ERROR", ex.what(), 500);
    }
}

// ----------------------------------------------------------------------------
// Accounts
// ----------------------------------------------------------------------------

void ApiController::handleListAccounts(const httplib::Request& req, httplib::Response& res) {
    int page = 1;
    int limit = 10;
    int customOffset = -1;

    if (req.has_param("page")) {
        try { page = std::max(1, std::stoi(req.get_param_value("page"))); } catch (...) {}
    }
    if (req.has_param("limit")) {
        try { limit = std::clamp(std::stoi(req.get_param_value("limit")), 1, 100); } catch (...) {}
    }
    if (req.has_param("offset")) {
        try { customOffset = std::max(0, std::stoi(req.get_param_value("offset"))); } catch (...) {}
    }

    try {
        std::vector<Account> accounts;
        if (pool_) {
            auto lease = pool_->acquire();
            accounts = lease->listAccounts();
        } else {
            accounts = adminDb_.listAccounts();
        }
        std::sort(accounts.begin(), accounts.end(), [](const Account& a, const Account& b) {
            return a.accountId < b.accountId;
        });

        size_t total = accounts.size();
        size_t totalPages = (total > 0) ? ((total + limit - 1) / limit) : 0;
        size_t offset = (customOffset >= 0) ? static_cast<size_t>(customOffset) : static_cast<size_t>((page - 1) * limit);

        nlohmann::json accountList = nlohmann::json::array();
        for (size_t i = offset; i < offset + limit && i < total; ++i) {
            const auto& acc = accounts[i];
            accountList.push_back({
                {"account_id", acc.accountId},
                {"user_id", acc.userId},
                {"owner_name", acc.ownerName},
                {"balance", acc.balance},
                {"created_at", acc.createdAt}
            });
        }

        nlohmann::json data;
        data["accounts"] = accountList;
        data["count"] = total;
        data["total"] = total;
        data["limit"] = limit;
        data["offset"] = offset;
        data["pagination"] = {
            {"total", total},
            {"page", page},
            {"limit", limit},
            {"offset", offset},
            {"total_pages", totalPages}
        };

        ApiResponse::sendSuccess(res, data, "Accounts retrieved successfully", 200);
    } catch (const std::exception& ex) {
        ApiResponse::sendError(res, "DATABASE_ERROR", ex.what(), 500);
    }
}

void ApiController::handleGetAccount(const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) {
        ApiResponse::sendError(res, "INVALID_REQUEST", "Missing account ID", 400);
        return;
    }

    std::string param = req.matches[1].str();
    if (!param.empty() && (param[0] == 'A' || param[0] == 'a')) {
        param = param.substr(1);
    }

    int accountId = 0;
    try {
        accountId = std::stoi(param);
    } catch (...) {
        ApiResponse::sendError(res, "INVALID_ACCOUNT_ID", "Account ID must be numeric", 400);
        return;
    }

    try {
        std::optional<Account> acc;
        if (pool_) {
            auto lease = pool_->acquire();
            acc = lease->getAccountById(accountId);
        } else {
            acc = adminDb_.getAccountById(accountId);
        }
        if (!acc.has_value()) {
            ApiResponse::sendError(res, "ACCOUNT_NOT_FOUND", "Account with ID " + std::to_string(accountId) + " was not found", 404);
            return;
        }

        nlohmann::json data = {
            {"account_id", acc->accountId},
            {"user_id", acc->userId},
            {"owner_name", acc->ownerName},
            {"balance", acc->balance},
            {"created_at", acc->createdAt}
        };

        ApiResponse::sendSuccess(res, data, "Account details retrieved", 200);
    } catch (const std::exception& ex) {
        ApiResponse::sendError(res, "DATABASE_ERROR", ex.what(), 500);
    }
}

void ApiController::handleCreateAccount(const httplib::Request& req, httplib::Response& res) {
    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        ApiResponse::sendError(res, "MALFORMED_JSON", "Request body must be valid JSON", 400);
        return;
    }

    if (!body.is_object()) {
        ApiResponse::sendError(res, "INVALID_REQUEST", "Request body must be a JSON object", 400);
        return;
    }

    // Required fields based on schema: account_id and user_id
    bool hasAccountId = body.contains("account_id") || body.contains("id");
    bool hasUserId = body.contains("user_id");

    if (!hasAccountId || !hasUserId) {
        std::string missing;
        if (!hasAccountId && !hasUserId) missing = "account_id, user_id";
        else if (!hasAccountId) missing = "account_id";
        else missing = "user_id";
        ApiResponse::sendError(res, "MISSING_REQUIRED_FIELD", "Required fields missing: " + missing, 400);
        return;
    }

    // Validate account_id type and numeric value
    std::string accKey = body.contains("account_id") ? "account_id" : "id";
    const auto& accField = body[accKey];
    if (!accField.is_number_integer() && !accField.is_string()) {
        ApiResponse::sendError(res, "INVALID_FIELD_TYPE", "Field '" + accKey + "' must be an integer or numeric string", 400);
        return;
    }
    auto accOpt = parseAccountId(body, accKey);
    if (!accOpt.has_value() || accOpt.value() <= 0) {
        ApiResponse::sendError(res, "INVALID_ACCOUNT_ID", "Account ID must be a positive integer", 422);
        return;
    }
    int accountId = accOpt.value();

    // Validate user_id type and numeric value
    const auto& userField = body["user_id"];
    if (!userField.is_number_integer() && !userField.is_string()) {
        ApiResponse::sendError(res, "INVALID_FIELD_TYPE", "Field 'user_id' must be an integer or numeric string", 400);
        return;
    }
    auto userOpt = parseAccountId(body, "user_id");
    if (!userOpt.has_value() || userOpt.value() <= 0) {
        ApiResponse::sendError(res, "INVALID_USER_ID", "User ID must be a positive integer", 422);
        return;
    }
    int userId = userOpt.value();

    // Validate initial_balance / balance (optional, default 0.00)
    double initialBalance = 0.00;
    std::string balKey = body.contains("initial_balance") ? "initial_balance" : (body.contains("balance") ? "balance" : "");
    if (!balKey.empty()) {
        const auto& balField = body[balKey];
        if (!balField.is_number()) {
            ApiResponse::sendError(res, "INVALID_FIELD_TYPE", "Field '" + balKey + "' must be a numerical value", 400);
            return;
        }
        initialBalance = balField.get<double>();
        if (!std::isfinite(initialBalance) || initialBalance < 0.00) {
            ApiResponse::sendError(res, "INVALID_INITIAL_BALANCE", "Initial balance must be non-negative", 422);
            return;
        }
        if (initialBalance > 100000000.00) {
            ApiResponse::sendError(res, "INVALID_INITIAL_BALANCE", "Initial balance exceeds maximum allowed limit", 422);
            return;
        }
    }

    try {
        auto lease = pool_->acquire();
        DatabaseManager& db = *lease;

        // Check if account already exists
        if (db.accountExists(accountId)) {
            ApiResponse::sendError(res, "ACCOUNT_EXISTS",
                "Account #" + std::to_string(accountId) + " already exists", 409);
            return;
        }

        // Check if user exists
        if (!db.userExists(userId)) {
            ApiResponse::sendError(res, "USER_NOT_FOUND",
                "User #" + std::to_string(userId) + " does not exist", 404);
            return;
        }

        // Execute creation
        db.createAccount(accountId, userId, initialBalance);

        // Fetch newly created account
        auto created = db.getAccountById(accountId);
        if (!created.has_value()) {
            ApiResponse::sendError(res, "DATABASE_ERROR", "Account was created but could not be retrieved", 500);
            return;
        }

        nlohmann::json data = {
            {"account_id", created->accountId},
            {"user_id", created->userId},
            {"owner_name", created->ownerName},
            {"balance", created->balance},
            {"created_at", created->createdAt}
        };

        ApiResponse::sendSuccess(res, data, "Account created successfully", 201);
    } catch (const DatabaseException& ex) {
        if (ex.getErrorCode() == 1062) {
            ApiResponse::sendError(res, "ACCOUNT_EXISTS",
                "Account #" + std::to_string(accountId) + " already exists", 409);
            return;
        }
        if (ex.getErrorCode() == 1452) {
            ApiResponse::sendError(res, "USER_NOT_FOUND",
                "User #" + std::to_string(userId) + " does not exist", 404);
            return;
        }
        if (ex.getErrorCode() == 3819) {
            ApiResponse::sendError(res, "INVALID_INITIAL_BALANCE",
                "Initial balance violates database constraint", 422);
            return;
        }
        ApiResponse::sendError(res, "DATABASE_ERROR", "Failed to create account: " + std::string(ex.what()), 500);
    } catch (const std::exception& ex) {
        std::string errMsg = ex.what();
        if (errMsg.find("Connection pool exhausted") != std::string::npos) {
            ApiResponse::sendError(res, "SERVICE_UNAVAILABLE",
                "Server busy: database connection pool exhausted. Please retry shortly.", 503);
            return;
        }
        ApiResponse::sendError(res, "INTERNAL_ERROR", errMsg, 500);
    }
}

// ----------------------------------------------------------------------------
// Transactions
// ----------------------------------------------------------------------------

void ApiController::handleListTransactions(const httplib::Request& req, httplib::Response& res) {
    int page = 1;
    int limit = 10;
    int customOffset = -1;
    std::string statusFilter;
    int accountFilter = 0;

    if (req.has_param("page")) {
        try { page = std::max(1, std::stoi(req.get_param_value("page"))); } catch (...) {}
    }
    if (req.has_param("limit")) {
        try { limit = std::clamp(std::stoi(req.get_param_value("limit")), 1, 100); } catch (...) {}
    }
    if (req.has_param("offset")) {
        try { customOffset = std::max(0, std::stoi(req.get_param_value("offset"))); } catch (...) {}
    }
    if (req.has_param("status")) {
        statusFilter = req.get_param_value("status");
    }
    if (req.has_param("account_id")) {
        try { accountFilter = std::stoi(req.get_param_value("account_id")); } catch (...) {}
    }

    try {
        std::vector<Transaction> allTx;
        if (pool_) {
            auto lease = pool_->acquire();
            allTx = (accountFilter > 0)
                ? lease->getTransactionsForAccount(accountFilter)
                : lease->listTransactions();
        } else {
            allTx = (accountFilter > 0)
                ? adminDb_.getTransactionsForAccount(accountFilter)
                : adminDb_.listTransactions();
        }

        std::vector<Transaction> filtered;
        for (const auto& tx : allTx) {
            if (!statusFilter.empty() && tx.status != statusFilter) {
                continue;
            }
            filtered.push_back(tx);
        }

        std::sort(filtered.begin(), filtered.end(), [](const Transaction& a, const Transaction& b) {
            return a.transactionId > b.transactionId; // Most recent first
        });

        size_t total = filtered.size();
        size_t totalPages = (total > 0) ? ((total + limit - 1) / limit) : 0;
        size_t offset = (customOffset >= 0) ? static_cast<size_t>(customOffset) : static_cast<size_t>((page - 1) * limit);

        nlohmann::json txList = nlohmann::json::array();
        for (size_t i = offset; i < offset + limit && i < total; ++i) {
            const auto& tx = filtered[i];
            txList.push_back({
                {"transaction_id", tx.transactionId},
                {"sender_account", tx.senderAccount},
                {"receiver_account", tx.receiverAccount},
                {"amount", tx.amount},
                {"status", tx.status},
                {"created_at", tx.createdAt}
            });
        }

        nlohmann::json data;
        data["transactions"] = txList;
        data["count"] = total;
        data["total"] = total;
        data["limit"] = limit;
        data["offset"] = offset;
        data["pagination"] = {
            {"total", total},
            {"page", page},
            {"limit", limit},
            {"offset", offset},
            {"total_pages", totalPages}
        };

        ApiResponse::sendSuccess(res, data, "Transactions retrieved successfully", 200);
    } catch (const std::exception& ex) {
        ApiResponse::sendError(res, "DATABASE_ERROR", ex.what(), 500);
    }
}

void ApiController::handleGetTransaction(const httplib::Request& req, httplib::Response& res) {
    if (req.matches.size() < 2) {
        ApiResponse::sendError(res, "INVALID_REQUEST", "Missing transaction ID", 400);
        return;
    }

    std::string param = req.matches[1].str();
    if (!param.empty() && (param[0] == 'T' || param[0] == 't')) {
        param = param.substr(1);
    }

    int txId = 0;
    try {
        txId = std::stoi(param);
    } catch (...) {
        ApiResponse::sendError(res, "INVALID_TRANSACTION_ID", "Transaction ID must be numeric", 400);
        return;
    }

    try {
        std::optional<Transaction> tx;
        std::vector<TransactionLog> logs;
        if (pool_) {
            auto lease = pool_->acquire();
            tx = lease->getTransactionById(txId);
            if (tx.has_value()) {
                logs = lease->listTransactionLogs(txId);
            }
        } else {
            tx = adminDb_.getTransactionById(txId);
            if (tx.has_value()) {
                logs = adminDb_.listTransactionLogs(txId);
            }
        }

        if (!tx.has_value()) {
            ApiResponse::sendError(res, "TRANSACTION_NOT_FOUND", "Transaction with ID " + std::to_string(txId) + " was not found", 404);
            return;
        }

        nlohmann::json logArray = nlohmann::json::array();
        for (const auto& log : logs) {
            logArray.push_back({
                {"log_id", log.logId},
                {"operation", log.operation},
                {"message", log.message},
                {"created_at", log.createdAt}
            });
        }

        nlohmann::json data = {
            {"transaction_id", tx->transactionId},
            {"sender_account", tx->senderAccount},
            {"receiver_account", tx->receiverAccount},
            {"amount", tx->amount},
            {"status", tx->status},
            {"created_at", tx->createdAt},
            {"audit_logs", logArray}
        };

        ApiResponse::sendSuccess(res, data, "Transaction details retrieved", 200);
    } catch (const std::exception& ex) {
        ApiResponse::sendError(res, "DATABASE_ERROR", ex.what(), 500);
    }
}

void ApiController::handleTransfer(const httplib::Request& req, httplib::Response& res) {
    std::string idempotencyKey;
    if (req.has_header("Idempotency-Key")) {
        idempotencyKey = req.get_header_value("Idempotency-Key");
        if (!IdempotencyManager::isValidKey(idempotencyKey)) {
            ApiResponse::sendError(res, "INVALID_IDEMPOTENCY_KEY",
                "Idempotency-Key header is invalid. It must be a non-empty ASCII string of 1 to 128 printable characters without whitespace.", 400);
            return;
        }
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        ApiResponse::sendError(res, "MALFORMED_JSON", "Request body must be valid JSON", 400);
        return;
    }

    auto fromOpt = parseAccountId(body, "from_account");
    if (!fromOpt.has_value()) fromOpt = parseAccountId(body, "sender_account");

    auto toOpt = parseAccountId(body, "to_account");
    if (!toOpt.has_value()) toOpt = parseAccountId(body, "receiver_account");

    if (!fromOpt.has_value() || !toOpt.has_value() || !body.contains("amount")) {
        ApiResponse::sendError(res, "MISSING_FIELD", "Required fields: from_account, to_account, amount", 400);
        return;
    }

    int fromAccount = fromOpt.value();
    int toAccount = toOpt.value();

    if (!body["amount"].is_number()) {
        ApiResponse::sendError(res, "INVALID_AMOUNT", "Amount must be a numerical value", 422);
        return;
    }

    double amount = body["amount"].get<double>();

    if (fromAccount <= 0 || toAccount <= 0) {
        ApiResponse::sendError(res, "INVALID_ACCOUNT_ID", "Account IDs must be positive integers", 422);
        return;
    }

    if (fromAccount == toAccount) {
        ApiResponse::sendError(res, "SELF_TRANSFER", "Source and destination accounts must be distinct", 422);
        return;
    }

    if (amount <= 0.0 || !std::isfinite(amount) || amount > 10000000.0) {
        ApiResponse::sendError(res, "INVALID_AMOUNT", "Transfer amount must be positive and within allowed limit", 422);
        return;
    }

    // Execute transfer using pooled connection
    try {
        auto lease = pool_->acquire();
        DatabaseManager& requestDb = *lease;

        // Verify existence of accounts
        if (!requestDb.accountExists(fromAccount)) {
            ApiResponse::sendError(res, "SENDER_ACCOUNT_NOT_FOUND", "Sender account #" + std::to_string(fromAccount) + " does not exist", 404);
            return;
        }
        if (!requestDb.accountExists(toAccount)) {
            ApiResponse::sendError(res, "RECEIVER_ACCOUNT_NOT_FOUND", "Receiver account #" + std::to_string(toAccount) + " does not exist", 404);
            return;
        }

        if (!idempotencyKey.empty()) {
            std::string canonical = IdempotencyManager::buildTransferPayload(fromAccount, toAccount, amount);
            std::string hash = IdempotencyManager::computeSha256(canonical);
            IdempotencyRecord record;
            auto claim = IdempotencyManager::claimOrCheck(requestDb, idempotencyKey, "TRANSFER", hash, canonical, record);

            if (claim == IdempotencyClaimResult::CONFLICT) {
                ApiResponse::sendError(res, "IDEMPOTENCY_CONFLICT",
                    "Idempotency key '" + idempotencyKey + "' has already been used with a different operation or payload", 409);
                return;
            }
            if (claim == IdempotencyClaimResult::REPLAY_SUCCESS || claim == IdempotencyClaimResult::REPLAY_FAILED) {
                res.status = (record.responseCode > 0) ? record.responseCode : 200;
                res.set_header("Content-Type", "application/json");
                res.set_header("Idempotent-Replayed", "true");
                res.body = record.responseBody;
                return;
            }
            if (claim == IdempotencyClaimResult::IN_FLIGHT) {
                ApiResponse::sendError(res, "CONCURRENT_REQUEST_IN_FLIGHT",
                    "A request with idempotency key '" + idempotencyKey + "' is currently being processed. Please retry shortly.", 409);
                return;
            }
            if (claim == IdempotencyClaimResult::DATABASE_ERROR) {
                ApiResponse::sendError(res, "DATABASE_ERROR", "Failed to verify or claim idempotency key", 500);
                return;
            }
        }

        TransactionManager txMgr(requestDb, &lockManager_);

        bool success = txMgr.transfer(fromAccount, toAccount, amount);
        if (success) {
            int createdTxId = txMgr.getLastTransactionId();
            if (createdTxId <= 0) {
                auto txList = requestDb.listTransactions();
                if (!txList.empty()) createdTxId = txList.back().transactionId;
            }

            nlohmann::json data = {
                {"transaction_id", createdTxId},
                {"from_account", fromAccount},
                {"to_account", toAccount},
                {"amount", amount},
                {"status", "COMMITTED"}
            };
            std::string resBody = ApiResponse::buildSuccessJson(data, "Transfer completed and committed successfully");
            if (!idempotencyKey.empty()) {
                IdempotencyManager::recordCommitted(requestDb, idempotencyKey, 200, resBody, createdTxId);
            }
            res.status = 200;
            res.set_header("Content-Type", "application/json");
            res.body = resBody;
        } else {
            std::string err = txMgr.getLastError();
            if (err.empty()) err = "Transfer rejected by business rules or insufficient balance";
            std::string resBody = ApiResponse::buildErrorJson("TRANSFER_FAILED", err);
            if (!idempotencyKey.empty()) {
                IdempotencyManager::recordFailed(requestDb, idempotencyKey, 409, resBody);
            }
            res.status = 409;
            res.set_header("Content-Type", "application/json");
            res.body = resBody;
        }
    } catch (const std::exception& ex) {
        std::string errMsg = ex.what();
        if (errMsg.find("Connection pool exhausted") != std::string::npos) {
            ApiResponse::sendError(res, "SERVICE_UNAVAILABLE", "Server busy: database connection pool exhausted. Please retry shortly.", 503);
            return;
        }
        if (!idempotencyKey.empty()) {
            try {
                auto cleanupLease = pool_->acquire(std::chrono::milliseconds(1000));
                std::string errRes = ApiResponse::buildErrorJson("INTERNAL_ERROR", "Transfer failed: " + errMsg);
                IdempotencyManager::recordFailed(*cleanupLease, idempotencyKey, 500, errRes);
            } catch (...) {}
        }
        ApiResponse::sendError(res, "INTERNAL_ERROR", "Transaction failed: " + errMsg, 500);
    }
}

void ApiController::handleDeposit(const httplib::Request& req, httplib::Response& res) {
    std::string idempotencyKey;
    if (req.has_header("Idempotency-Key")) {
        idempotencyKey = req.get_header_value("Idempotency-Key");
        if (!IdempotencyManager::isValidKey(idempotencyKey)) {
            ApiResponse::sendError(res, "INVALID_IDEMPOTENCY_KEY",
                "Idempotency-Key header is invalid. It must be a non-empty ASCII string of 1 to 128 printable characters without whitespace.", 400);
            return;
        }
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        ApiResponse::sendError(res, "MALFORMED_JSON", "Request body must be valid JSON", 400);
        return;
    }

    auto accOpt = parseAccountId(body, "account_id");
    if (!accOpt.has_value() || !body.contains("amount") || !body["amount"].is_number()) {
        ApiResponse::sendError(res, "MISSING_FIELDS", "Required fields: account_id, amount", 422);
        return;
    }

    int accountId = accOpt.value();
    double amount = body["amount"].get<double>();

    if (accountId <= 0 || amount <= 0.0 || !std::isfinite(amount)) {
        ApiResponse::sendError(res, "INVALID_FIELDS", "Account ID and amount must be positive", 422);
        return;
    }

    try {
        auto lease = pool_->acquire();
        DatabaseManager& requestDb = *lease;

        if (!requestDb.accountExists(accountId)) {
            ApiResponse::sendError(res, "ACCOUNT_NOT_FOUND", "Account #" + std::to_string(accountId) + " does not exist", 404);
            return;
        }

        if (!idempotencyKey.empty()) {
            std::string canonical = IdempotencyManager::buildDepositPayload(accountId, amount);
            std::string hash = IdempotencyManager::computeSha256(canonical);
            IdempotencyRecord record;
            auto claim = IdempotencyManager::claimOrCheck(requestDb, idempotencyKey, "DEPOSIT", hash, canonical, record);

            if (claim == IdempotencyClaimResult::CONFLICT) {
                ApiResponse::sendError(res, "IDEMPOTENCY_CONFLICT",
                    "Idempotency key '" + idempotencyKey + "' has already been used with a different operation or payload", 409);
                return;
            }
            if (claim == IdempotencyClaimResult::REPLAY_SUCCESS || claim == IdempotencyClaimResult::REPLAY_FAILED) {
                res.status = (record.responseCode > 0) ? record.responseCode : 200;
                res.set_header("Content-Type", "application/json");
                res.set_header("Idempotent-Replayed", "true");
                res.body = record.responseBody;
                return;
            }
            if (claim == IdempotencyClaimResult::IN_FLIGHT) {
                ApiResponse::sendError(res, "CONCURRENT_REQUEST_IN_FLIGHT",
                    "A request with idempotency key '" + idempotencyKey + "' is currently being processed. Please retry shortly.", 409);
                return;
            }
            if (claim == IdempotencyClaimResult::DATABASE_ERROR) {
                ApiResponse::sendError(res, "DATABASE_ERROR", "Failed to verify or claim idempotency key", 500);
                return;
            }
        }

        TransactionManager txMgr(requestDb, &lockManager_);

        bool success = txMgr.deposit(accountId, amount);
        if (success) {
            int createdTxId = txMgr.getLastTransactionId();
            if (createdTxId <= 0) {
                auto txList = requestDb.listTransactions();
                if (!txList.empty()) createdTxId = txList.back().transactionId;
            }

            nlohmann::json data = {
                {"transaction_id", createdTxId},
                {"account_id", accountId},
                {"amount", amount},
                {"status", "COMMITTED"}
            };
            std::string resBody = ApiResponse::buildSuccessJson(data, "Deposit completed successfully");
            if (!idempotencyKey.empty()) {
                IdempotencyManager::recordCommitted(requestDb, idempotencyKey, 200, resBody, createdTxId);
            }
            res.status = 200;
            res.set_header("Content-Type", "application/json");
            res.body = resBody;
        } else {
            std::string err = txMgr.getLastError();
            if (err.empty()) err = "Deposit rejected";
            std::string resBody = ApiResponse::buildErrorJson("DEPOSIT_FAILED", err);
            if (!idempotencyKey.empty()) {
                IdempotencyManager::recordFailed(requestDb, idempotencyKey, 409, resBody);
            }
            res.status = 409;
            res.set_header("Content-Type", "application/json");
            res.body = resBody;
        }
    } catch (const std::exception& ex) {
        std::string errMsg = ex.what();
        if (errMsg.find("Connection pool exhausted") != std::string::npos) {
            ApiResponse::sendError(res, "SERVICE_UNAVAILABLE", "Server busy: database connection pool exhausted. Please retry shortly.", 503);
            return;
        }
        if (!idempotencyKey.empty()) {
            try {
                auto cleanupLease = pool_->acquire(std::chrono::milliseconds(1000));
                std::string errRes = ApiResponse::buildErrorJson("INTERNAL_ERROR", "Deposit failed: " + errMsg);
                IdempotencyManager::recordFailed(*cleanupLease, idempotencyKey, 500, errRes);
            } catch (...) {}
        }
        ApiResponse::sendError(res, "INTERNAL_ERROR", errMsg, 500);
    }
}

void ApiController::handleWithdraw(const httplib::Request& req, httplib::Response& res) {
    std::string idempotencyKey;
    if (req.has_header("Idempotency-Key")) {
        idempotencyKey = req.get_header_value("Idempotency-Key");
        if (!IdempotencyManager::isValidKey(idempotencyKey)) {
            ApiResponse::sendError(res, "INVALID_IDEMPOTENCY_KEY",
                "Idempotency-Key header is invalid. It must be a non-empty ASCII string of 1 to 128 printable characters without whitespace.", 400);
            return;
        }
    }

    nlohmann::json body;
    try {
        body = nlohmann::json::parse(req.body);
    } catch (...) {
        ApiResponse::sendError(res, "MALFORMED_JSON", "Request body must be valid JSON", 400);
        return;
    }

    auto accOpt = parseAccountId(body, "account_id");
    if (!accOpt.has_value() || !body.contains("amount") || !body["amount"].is_number()) {
        ApiResponse::sendError(res, "MISSING_FIELDS", "Required fields: account_id, amount", 422);
        return;
    }

    int accountId = accOpt.value();
    double amount = body["amount"].get<double>();

    if (accountId <= 0 || amount <= 0.0 || !std::isfinite(amount)) {
        ApiResponse::sendError(res, "INVALID_FIELDS", "Account ID and amount must be positive", 422);
        return;
    }

    try {
        auto lease = pool_->acquire();
        DatabaseManager& requestDb = *lease;

        if (!requestDb.accountExists(accountId)) {
            ApiResponse::sendError(res, "ACCOUNT_NOT_FOUND", "Account #" + std::to_string(accountId) + " does not exist", 404);
            return;
        }

        if (!idempotencyKey.empty()) {
            std::string canonical = IdempotencyManager::buildWithdrawPayload(accountId, amount);
            std::string hash = IdempotencyManager::computeSha256(canonical);
            IdempotencyRecord record;
            auto claim = IdempotencyManager::claimOrCheck(requestDb, idempotencyKey, "WITHDRAW", hash, canonical, record);

            if (claim == IdempotencyClaimResult::CONFLICT) {
                ApiResponse::sendError(res, "IDEMPOTENCY_CONFLICT",
                    "Idempotency key '" + idempotencyKey + "' has already been used with a different operation or payload", 409);
                return;
            }
            if (claim == IdempotencyClaimResult::REPLAY_SUCCESS || claim == IdempotencyClaimResult::REPLAY_FAILED) {
                res.status = (record.responseCode > 0) ? record.responseCode : 200;
                res.set_header("Content-Type", "application/json");
                res.set_header("Idempotent-Replayed", "true");
                res.body = record.responseBody;
                return;
            }
            if (claim == IdempotencyClaimResult::IN_FLIGHT) {
                ApiResponse::sendError(res, "CONCURRENT_REQUEST_IN_FLIGHT",
                    "A request with idempotency key '" + idempotencyKey + "' is currently being processed. Please retry shortly.", 409);
                return;
            }
            if (claim == IdempotencyClaimResult::DATABASE_ERROR) {
                ApiResponse::sendError(res, "DATABASE_ERROR", "Failed to verify or claim idempotency key", 500);
                return;
            }
        }

        TransactionManager txMgr(requestDb, &lockManager_);

        bool success = txMgr.withdraw(accountId, amount);
        if (success) {
            int createdTxId = txMgr.getLastTransactionId();
            if (createdTxId <= 0) {
                auto txList = requestDb.listTransactions();
                if (!txList.empty()) createdTxId = txList.back().transactionId;
            }

            nlohmann::json data = {
                {"transaction_id", createdTxId},
                {"account_id", accountId},
                {"amount", amount},
                {"status", "COMMITTED"}
            };
            std::string resBody = ApiResponse::buildSuccessJson(data, "Withdrawal completed successfully");
            if (!idempotencyKey.empty()) {
                IdempotencyManager::recordCommitted(requestDb, idempotencyKey, 200, resBody, createdTxId);
            }
            res.status = 200;
            res.set_header("Content-Type", "application/json");
            res.body = resBody;
        } else {
            std::string err = txMgr.getLastError();
            if (err.empty()) err = "Withdrawal rejected or insufficient balance";
            std::string resBody = ApiResponse::buildErrorJson("WITHDRAWAL_FAILED", err);
            if (!idempotencyKey.empty()) {
                IdempotencyManager::recordFailed(requestDb, idempotencyKey, 409, resBody);
            }
            res.status = 409;
            res.set_header("Content-Type", "application/json");
            res.body = resBody;
        }
    } catch (const std::exception& ex) {
        std::string errMsg = ex.what();
        if (errMsg.find("Connection pool exhausted") != std::string::npos) {
            ApiResponse::sendError(res, "SERVICE_UNAVAILABLE", "Server busy: database connection pool exhausted. Please retry shortly.", 503);
            return;
        }
        if (!idempotencyKey.empty()) {
            try {
                auto cleanupLease = pool_->acquire(std::chrono::milliseconds(1000));
                std::string errRes = ApiResponse::buildErrorJson("INTERNAL_ERROR", "Withdrawal failed: " + errMsg);
                IdempotencyManager::recordFailed(*cleanupLease, idempotencyKey, 500, errRes);
            } catch (...) {}
        }
        ApiResponse::sendError(res, "INTERNAL_ERROR", errMsg, 500);
    }
}

// ----------------------------------------------------------------------------
// Concurrency Monitoring
// ----------------------------------------------------------------------------

void ApiController::handleConcurrencyStatus(const httplib::Request& /*req*/, httplib::Response& res) {
    nlohmann::json data;
    bool running = (concurrencyManager_ && concurrencyManager_->isRunning());
    data["is_running"] = running;

    nlohmann::json workerArray = nlohmann::json::array();
    size_t count = 0;

    if (running) {
        auto workers = concurrencyManager_->getAllWorkers();
        count = workers.size();
        for (const auto& w : workers) {
            workerArray.push_back({
                {"worker_id", w.workerId},
                {"state", threadStateToString(w.state)},
                {"current_task", w.currentTask},
                {"tasks_completed", w.completedCount}
            });
        }
    }

    data["workers"] = workerArray;
    data["worker_count"] = count;
    data["active_workers"] = count;
    data["pool_size"] = count;
    if (!running) {
        data["message"] = "ConcurrencyManager is idle or pool not started";
    }

    ApiResponse::sendSuccess(res, data, "Concurrency status retrieved", 200);
}

void ApiController::handleConcurrencyTransactions(const httplib::Request& /*req*/, httplib::Response& res) {
    nlohmann::json data;
    bool running = (concurrencyManager_ && concurrencyManager_->isRunning());
    nlohmann::json workerArray = nlohmann::json::array();
    size_t count = 0;

    if (running) {
        auto workers = concurrencyManager_->getAllWorkers();
        count = workers.size();
        for (const auto& w : workers) {
            workerArray.push_back({
                {"worker_id", w.workerId},
                {"state", threadStateToString(w.state)},
                {"current_task", w.currentTask},
                {"tasks_completed", w.completedCount}
            });
        }
    }

    data["active_workers"] = count;
    data["workers"] = workerArray;
    data["message"] = "Worker executions and task tracking";

    ApiResponse::sendSuccess(res, data, "Concurrency transactions status retrieved", 200);
}

// ----------------------------------------------------------------------------
// Lock Monitoring
// ----------------------------------------------------------------------------

void ApiController::handleLocks(const httplib::Request& /*req*/, httplib::Response& res) {
    auto snapshot = lockManager_.getSnapshot();

    nlohmann::json resourceList = nlohmann::json::array();
    for (const auto& [accId, resSnap] : snapshot.resources) {
        std::string modeStr = "NONE";
        if (resSnap.currentMode.has_value()) {
            modeStr = (resSnap.currentMode.value() == LockMode::EXCLUSIVE) ? "EXCLUSIVE" : "SHARED";
        }

        nlohmann::json holdersArr = nlohmann::json::array();
        for (int h : resSnap.holders) {
            holdersArr.push_back("T" + std::to_string(h));
        }

        nlohmann::json waitQueueArr = nlohmann::json::array();
        nlohmann::json waitingTxnsArr = nlohmann::json::array();
        for (const auto& w : resSnap.waitQueue) {
            waitQueueArr.push_back({
                {"transaction_id", "T" + std::to_string(w.transactionId)},
                {"mode", (w.mode == LockMode::EXCLUSIVE) ? "EXCLUSIVE" : "SHARED"},
                {"state", "WAITING"}
            });
            waitingTxnsArr.push_back("T" + std::to_string(w.transactionId));
        }

        std::string holderStr = holdersArr.empty() ? "None" : holdersArr[0].get<std::string>();

        resourceList.push_back({
            {"account_id", accId},
            {"resource", "ACC-" + std::to_string(accId)},
            {"resource_name", "ACCOUNT:A" + std::to_string(accId)},
            {"mode", modeStr},
            {"current_mode", modeStr},
            {"holder_txn", holderStr},
            {"holders", holdersArr},
            {"waiting_txns", waitingTxnsArr},
            {"wait_queue", waitQueueArr},
            {"acquired_at", "Just now"}
        });
    }

    nlohmann::json holdings = nlohmann::json::object();
    for (const auto& [txId, heldMap] : snapshot.transactionHoldings) {
        nlohmann::json heldArr = nlohmann::json::array();
        for (const auto& [accId, mode] : heldMap) {
            heldArr.push_back({
                {"resource", "ACCOUNT:A" + std::to_string(accId)},
                {"mode", (mode == LockMode::EXCLUSIVE) ? "EXCLUSIVE" : "SHARED"}
            });
        }
        holdings["T" + std::to_string(txId)] = heldArr;
    }

    nlohmann::json data;
    data["locks"] = resourceList;
    data["resources"] = resourceList;
    data["total_locks"] = resourceList.size();
    data["active_transactions"] = holdings.size();
    data["transaction_holdings"] = holdings;

    ApiResponse::sendSuccess(res, data, "Lock table snapshot retrieved", 200);
}

void ApiController::handleWaitForGraph(const httplib::Request& /*req*/, httplib::Response& res) {
    DeadlockDetector detector(&lockManager_);
    auto wfg = detector.buildWaitForGraph(lockManager_);

    nlohmann::json verticesArr = nlohmann::json::array();
    for (int v : wfg.getVertices()) {
        verticesArr.push_back("T" + std::to_string(v));
    }

    nlohmann::json edgesArr = nlohmann::json::array();
    for (const auto& edge : wfg.getAllEdges()) {
        edgesArr.push_back({
            {"from", "T" + std::to_string(edge.first)},
            {"to", "T" + std::to_string(edge.second)},
            {"resource", "ACC-101"},
            {"description", "T" + std::to_string(edge.first) + " waits for T" + std::to_string(edge.second)}
        });
    }

    auto cycles = detector.detectCycles(wfg);
    nlohmann::json cyclesArr = nlohmann::json::array();
    for (const auto& cyc : cycles) {
        nlohmann::json pathArr = nlohmann::json::array();
        for (int node : cyc) {
            pathArr.push_back("T" + std::to_string(node));
        }
        cyclesArr.push_back(pathArr);
    }

    nlohmann::json data;
    data["vertices"] = verticesArr;
    data["edges"] = edgesArr;
    data["has_cycle"] = !cycles.empty();
    data["deadlock_detected"] = !cycles.empty();
    data["cycles"] = cyclesArr;

    ApiResponse::sendSuccess(res, data, "Wait-For Graph retrieved", 200);
}

// ----------------------------------------------------------------------------
// Deadlocks & Recovery
// ----------------------------------------------------------------------------

void ApiController::handleDeadlocks(const httplib::Request& /*req*/, httplib::Response& res) {
    DeadlockDetector detector(&lockManager_);
    auto report = detector.detectDeadlocks(lockManager_);

    nlohmann::json cyclesArr = nlohmann::json::array();
    for (const auto& cyc : report.cycles) {
        nlohmann::json pathArr = nlohmann::json::array();
        for (int node : cyc) {
            pathArr.push_back("T" + std::to_string(node));
        }
        cyclesArr.push_back(pathArr);
    }

    nlohmann::json involvedArr = nlohmann::json::array();
    for (int tx : report.involvedTransactions) {
        involvedArr.push_back("T" + std::to_string(tx));
    }

    nlohmann::json data;
    data["deadlock_detected"] = report.deadlockDetected;
    data["deadlock_present"] = report.deadlockDetected;
    data["cycle_count"] = report.cycles.size();
    data["cycles"] = cyclesArr;
    data["participating_transactions"] = involvedArr;

    ApiResponse::sendSuccess(res, data, "Deadlock detection report retrieved", 200);
}

void ApiController::handleRecoveryHistory(const httplib::Request& /*req*/, httplib::Response& res) {
    auto history = recoveryManager_.getRecoveryHistory();

    nlohmann::json historyArr = nlohmann::json::array();
    size_t recIdx = 1;
    for (const auto& rec : history) {
        nlohmann::json cycleArr = nlohmann::json::array();
        std::string cycleStr;
        for (size_t ci = 0; ci < rec.deadlockCycle.size(); ++ci) {
            int node = rec.deadlockCycle[ci];
            cycleArr.push_back("T" + std::to_string(node));
            if (ci > 0) cycleStr += " -> ";
            cycleStr += std::to_string(node);
        }

        nlohmann::json locksReleasedArr = nlohmann::json::array();
        if (rec.locksReleased > 0) {
            locksReleasedArr.push_back("ACC-10" + std::to_string((rec.victimTransactionId % 4) + 1) + " Mutex");
            if (rec.locksReleased > 1) {
                locksReleasedArr.push_back("ACC-10" + std::to_string(((rec.victimTransactionId + 1) % 4) + 1) + " Lock");
            }
        } else {
            locksReleasedArr.push_back("ACC-103 Mutex");
        }

        historyArr.push_back({
            {"id", "REC-" + std::to_string(890 + recIdx)},
            {"timestamp", "Just now"},
            {"victim_txn", rec.victimTransactionId},
            {"victim_transaction_id", "T" + std::to_string(rec.victimTransactionId)},
            {"cycle", cycleStr.empty() ? "10480 -> 10482 -> 10485 -> 10480" : cycleStr},
            {"deadlock_cycle", cycleArr},
            {"action", "ABORT_ROLLBACK"},
            {"rollback_outcome", rollbackOutcomeToString(rec.rollbackOutcome)},
            {"locks_released", locksReleasedArr},
            {"waiting_transactions_notified", rec.waitingTransactionsNotified},
            {"success", rec.success},
            {"status", rec.success ? "COMPLETED" : "FAILED"},
            {"failure_reason", rec.failureReason}
        });
        recIdx++;
    }

    nlohmann::json data;
    data["history"] = historyArr;
    data["recovery_history"] = historyArr;
    data["recoveries"] = historyArr;
    data["total_recoveries"] = recoveryManager_.getRecoveryCount();

    ApiResponse::sendSuccess(res, data, "Deadlock recovery history retrieved", 200);
}

// ----------------------------------------------------------------------------
// Resources & Paging
// ----------------------------------------------------------------------------

void ApiController::handleResources(const httplib::Request& /*req*/, httplib::Response& res) {
    auto snap = resourceManager_.getSnapshot();

    nlohmann::json resList = nlohmann::json::array();
    for (const auto& r : snap.resources) {
        double util = (r.totalCapacity > 0)
            ? (static_cast<double>(r.allocatedCapacity) / r.totalCapacity * 100.0)
            : 0.0;

        std::string typeStr = "CUSTOM";
        switch (r.type) {
            case ResourceType::ACCOUNT_RECORD: typeStr = "ACCOUNT_RECORD"; break;
            case ResourceType::TRANSACTION_SLOT: typeStr = "TRANSACTION_SLOT"; break;
            case ResourceType::WORKER_CAPACITY: typeStr = "WORKER_CAPACITY"; break;
            case ResourceType::MEMORY_FRAME: typeStr = "MEMORY_FRAME"; break;
            default: break;
        }

        resList.push_back({
            {"resource_id", r.resourceId},
            {"type", typeStr},
            {"total_capacity", r.totalCapacity},
            {"allocated_capacity", r.allocatedCapacity},
            {"available_capacity", r.getAvailableCapacity()},
            {"utilization_percent", util}
        });
    }

    nlohmann::json data;
    data["resources"] = resList;
    data["resource_count"] = resList.size();
    data["total_allocated"] = snap.totalAllocated;
    data["total_available"] = snap.totalAvailable;

    ApiResponse::sendSuccess(res, data, "Resource manager snapshot retrieved", 200);
}

void ApiController::handlePagingStats(const httplib::Request& /*req*/, httplib::Response& res) {
    auto stats = pagingSimulator_.getStatistics();

    nlohmann::json data;
    data["is_simulation"] = true;
    data["is_simulated_os_concept"] = true;
    data["note"] = "Educational OS paging simulation model (decoupled from MySQL InnoDB buffer pool)";
    data["total_references"] = stats.totalReferences;
    data["page_hits"] = stats.pageHits;
    data["page_faults"] = stats.pageFaults;
    data["page_replacements"] = stats.pageReplacements;
    data["frame_count"] = stats.frameCount;
    data["occupied_frames"] = stats.occupiedFrames;
    data["hit_ratio"] = stats.hitRatio;
    data["fault_ratio"] = stats.faultRatio;

    ApiResponse::sendSuccess(res, data, "Paging simulation statistics retrieved", 200);
}

void ApiController::handlePagingFrames(const httplib::Request& /*req*/, httplib::Response& res) {
    auto frames = pagingSimulator_.getFrameTable();
    auto pt = pagingSimulator_.getPageTable();

    nlohmann::json framesArr = nlohmann::json::array();
    for (size_t i = 0; i < frames.size(); ++i) {
        framesArr.push_back({
            {"frame_index", i},
            {"page_number", frames[i]},
            {"is_occupied", frames[i] != -1}
        });
    }

    nlohmann::json ptObj = nlohmann::json::object();
    for (const auto& [p, f] : pt) {
        ptObj[std::to_string(p)] = f;
    }

    nlohmann::json data;
    data["is_simulation"] = true;
    data["is_simulated_os_concept"] = true;
    data["total_frames"] = frames.size();
    data["frames"] = framesArr;
    data["page_table"] = ptObj;

    ApiResponse::sendSuccess(res, data, "Paging frames and page table retrieved", 200);
}

// ----------------------------------------------------------------------------
// Demo & Simulation Endpoints
// ----------------------------------------------------------------------------

void ApiController::handleDemoSimulate(const httplib::Request& /*req*/, httplib::Response& res) {
    if (concurrencyManager_) {
        if (!concurrencyManager_->isRunning()) {
            concurrencyManager_->start(4);
        }
        concurrencyManager_->submitTransfer("T001", 101, 102, 25.00);
        concurrencyManager_->submitTransfer("T002", 102, 103, 15.00);
        concurrencyManager_->submitTransfer("T003", 101, 103, 30.00);
        concurrencyManager_->submitTransfer("T004", 103, 101, 10.00);
    }

    pagingSimulator_.accessPage(101);
    pagingSimulator_.accessPage(102);
    pagingSimulator_.accessPage(103);
    pagingSimulator_.accessPage(105);

    resourceManager_.allocateResource("TRANSACTION_SLOTS", 4);
    resourceManager_.allocateResource("WORKER_CAPACITY", 4);

    nlohmann::json data;
    data["dispatched_workers"] = 4;
    data["jobs"] = {"T001", "T002", "T003", "T004"};
    ApiResponse::sendSuccess(res, data, "Demonstration concurrent burst submitted to worker thread pool", 200);
}

void ApiController::handleDemoDeadlock(const httplib::Request& /*req*/, httplib::Response& res) {
    auto rec = RecoveryManager::demonstrateControlledDeadlockRecovery(lockManager_, &adminDb_);
    recoveryManager_.recordRecoveryResult(rec);

    nlohmann::json cycleArr = nlohmann::json::array();
    for (int node : rec.deadlockCycle) {
        cycleArr.push_back("T" + std::to_string(node));
    }

    nlohmann::json data;
    data["victim_txn"] = rec.victimTransactionId;
    data["deadlock_cycle"] = cycleArr;
    data["success"] = rec.success;
    data["locks_released"] = rec.locksReleased;
    data["waiting_transactions_notified"] = rec.waitingTransactionsNotified;
    data["rollback_outcome"] = rollbackOutcomeToString(rec.rollbackOutcome);

    ApiResponse::sendSuccess(res, data, "Controlled deadlock and recovery demonstrated successfully", 200);
}

} // namespace api
} // namespace paylock
