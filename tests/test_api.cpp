#include "api/HttpServer.h"
#include "database/DatabaseManager.h"
#include "transaction/TransactionManager.h"
#include "concurrency/ConcurrencyManager.h"
#include "locking/LockManager.h"
#include "deadlock/DeadlockDetector.h"
#include "recovery/RecoveryManager.h"
#include "resources/ResourceManager.h"
#include "resources/PagingSimulator.h"
#include "logging/Logger.h"

#include <iostream>
#include <iomanip>
#include <cassert>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>
#include <httplib.h>

using json = nlohmann::json;

namespace {

int g_passedTests = 0;
int g_totalTests = 0;

void runTest(const std::string& testName, const std::function<void()>& fn) {
    g_totalTests++;
    std::cout << "Test " << g_totalTests << ": " << testName << "... " << std::flush;
    try {
        fn();
        g_passedTests++;
        std::cout << "PASSED\n";
    } catch (const std::exception& ex) {
        std::cout << "FAILED: " << ex.what() << "\n";
        assert(false);
    }
}

void resetTestDatabase(paylock::DatabaseManager& db) {
    try {
        db.executeQuery("DELETE FROM transaction_logs;");
        db.executeQuery("DELETE FROM transactions;");
        db.executeQuery("ALTER TABLE transactions AUTO_INCREMENT = 1001;");
        db.executeQuery("ALTER TABLE transaction_logs AUTO_INCREMENT = 1;");
        db.executeQuery("UPDATE accounts SET balance = 10000.00 WHERE account_id = 101;");
        db.executeQuery("UPDATE accounts SET balance = 5000.00 WHERE account_id = 102;");
        db.executeQuery("UPDATE accounts SET balance = 7000.00 WHERE account_id = 103;");
    } catch (const std::exception& ex) {
        std::cerr << "Warning during DB reset: " << ex.what() << "\n";
    }
}

} // namespace

int main() {
    std::cout << "\n=======================================================\n";
    std::cout << "      PAYLOCK STEP 8 — HTTP REST API TEST SUITE        \n";
    std::cout << "=======================================================\n\n";

    paylock::DatabaseConfig dbConfig = paylock::DatabaseConfig::fromEnvOrDefault();
    paylock::DatabaseManager adminDb(dbConfig);
    adminDb.connect();

    if (!adminDb.isConnected()) {
        std::cerr << "[CRITICAL ERROR]: Could not connect to MySQL database for API testing.\n";
        return 1;
    }

    resetTestDatabase(adminDb);

    // Initialize core subsystems
    paylock::LockManager lockManager;
    paylock::TransactionManager txManager(adminDb, &lockManager);
    paylock::RecoveryManager recoveryManager(&lockManager, &txManager, &adminDb);
    paylock::ResourceManager resourceManager;
    paylock::PagingSimulator pagingSimulator(64);
    paylock::ConcurrencyManager concurrencyManager(dbConfig);

    // Run test server on ephemeral port (e.g. 18088)
    const int TEST_PORT = 18088;
    const std::string TEST_HOST = "127.0.0.1";

    paylock::api::HttpServer server(dbConfig,
                                    adminDb,
                                    lockManager,
                                    recoveryManager,
                                    resourceManager,
                                    pagingSimulator,
                                    &concurrencyManager,
                                    TEST_HOST,
                                    TEST_PORT);

    bool started = server.start(true);
    assert(started && "HTTP API server must start successfully in background thread");
    assert(server.isRunning() && "Server isRunning() must return true");

    httplib::Client client(TEST_HOST, TEST_PORT);
    client.set_connection_timeout(std::chrono::seconds(5));
    client.set_read_timeout(std::chrono::seconds(5));

    // ==========================================
    // 1. Health and System Status Tests
    // ==========================================

    runTest("[API Health] GET /api/v1/health returns 200 with DB status", [&]() {
        auto res = client.Get("/api/v1/health");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["server"] == "healthy");
        assert(j["data"]["database"]["connected"] == true);
        assert(j["data"]["version"] == "1.0.0");
    });

    runTest("[API Stats] GET /api/v1/system/stats returns metrics snapshot", [&]() {
        auto res = client.Get("/api/v1/system/stats");
        if (!res || res->status != 200) {
            std::cerr << "\n[DEBUG STATS FAILED]: res=" << (res ? std::to_string(res->status) : "null")
                      << " body=" << (res ? res->body : "") << "\n";
        }
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"].contains("transactions"));
        assert(j["data"].contains("locks"));
        assert(j["data"].contains("deadlocks"));
        assert(j["data"].contains("resources"));
        assert(j["data"].contains("paging_simulation"));
    });

    // ==========================================
    // 2. Accounts Endpoints Tests
    // ==========================================

    runTest("[API Accounts] GET /api/v1/accounts returns account list", [&]() {
        auto res = client.Get("/api/v1/accounts");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["accounts"].is_array());
        assert(j["data"]["accounts"].size() >= 3);
        assert(j["data"]["count"] >= 3);
    });

    runTest("[API Accounts] GET /api/v1/accounts with pagination limit and offset", [&]() {
        auto res = client.Get("/api/v1/accounts?limit=2&offset=1");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["accounts"].size() <= 2);
        assert(j["data"]["limit"] == 2);
        assert(j["data"]["offset"] == 1);
    });

    runTest("[API Accounts] GET /api/v1/accounts/:id existing account lookup", [&]() {
        auto res = client.Get("/api/v1/accounts/101");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["account_id"] == 101);
        assert(j["data"]["balance"] >= 0.0);
    });

    runTest("[API Accounts] GET /api/v1/accounts/:id nonexistent account returns 404", [&]() {
        auto res = client.Get("/api/v1/accounts/9999");
        assert(res != nullptr && res->status == 404);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "ACCOUNT_NOT_FOUND");
    });

    runTest("[API Accounts] GET /api/v1/accounts/:id invalid identifier returns 400", [&]() {
        auto res = client.Get("/api/v1/accounts/abc");
        assert(res != nullptr && res->status == 400);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "INVALID_ACCOUNT_ID");
    });

    // ==========================================
    // 3. Transactions Endpoints Tests
    // ==========================================

    runTest("[API Transfer] Valid transfer ₹500 from Account 101 -> 102 returns 200 COMMITTED", [&]() {
        json body = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", 500.0}
        };
        auto res = client.Post("/api/v1/transactions/transfer", body.dump(), "application/json");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["status"] == "COMMITTED");
        assert(j["data"]["amount"] == 500.0);
        assert(j["data"]["from_account"] == 101);
        assert(j["data"]["to_account"] == 102);

        // Verify database balances updated correctly
        auto b101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto b102 = adminDb.getAccountBalance(102).value_or(0.0);
        assert(b101 == 9500.0);
        assert(b102 == 5500.0);
    });

    runTest("[API Transfer] String-formatted account IDs (A101 -> A102) accepted & processed", [&]() {
        json body = {
            {"from_account", "A101"},
            {"to_account", "A102"},
            {"amount", 100.0}
        };
        auto res = client.Post("/api/v1/transactions/transfer", body.dump(), "application/json");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["status"] == "COMMITTED");
    });

    runTest("[API Transfer] Self-transfer (101 -> 101) rejected with 422", [&]() {
        json body = {
            {"from_account", 101},
            {"to_account", 101},
            {"amount", 100.0}
        };
        auto res = client.Post("/api/v1/transactions/transfer", body.dump(), "application/json");
        assert(res != nullptr && res->status == 422);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "SELF_TRANSFER");
    });

    runTest("[API Transfer] Non-positive amount rejected with 422", [&]() {
        json body = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", -50.0}
        };
        auto res = client.Post("/api/v1/transactions/transfer", body.dump(), "application/json");
        assert(res != nullptr && res->status == 422);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "INVALID_AMOUNT");
    });

    runTest("[API Transfer] Missing required fields rejected with 400", [&]() {
        json body = {
            {"from_account", 101},
            {"amount", 50.0}
        };
        auto res = client.Post("/api/v1/transactions/transfer", body.dump(), "application/json");
        assert(res != nullptr && res->status == 400);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "MISSING_FIELD");
    });

    runTest("[API Transfer] Malformed JSON rejected with 400", [&]() {
        auto res = client.Post("/api/v1/transactions/transfer", "{ invalid_json ", "application/json");
        assert(res != nullptr && res->status == 400);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "MALFORMED_JSON");
    });

    runTest("[API Transfer] Overdraft beyond balance triggers 409 Conflict & safe rollback", [&]() {
        json body = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", 999999.0}
        };
        auto res = client.Post("/api/v1/transactions/transfer", body.dump(), "application/json");
        assert(res != nullptr && res->status == 409);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "INSUFFICIENT_FUNDS" || j["error"]["code"] == "TRANSFER_FAILED");

        // Verify account balance was unchanged (Atomicity)
        auto b101 = adminDb.getAccountBalance(101).value_or(0.0);
        assert(b101 == 9400.0); // 10000 - 500 - 100 = 9400
    });

    runTest("[API Transfer] Transfer to nonexistent account returns 404", [&]() {
        json body = {
            {"from_account", 101},
            {"to_account", 9999},
            {"amount", 100.0}
        };
        auto res = client.Post("/api/v1/transactions/transfer", body.dump(), "application/json");
        assert(res != nullptr && (res->status == 404 || res->status == 422));
        auto j = json::parse(res->body);
        assert(j["success"] == false);
    });

    runTest("[API Deposit] POST /api/v1/transactions/deposit credits account", [&]() {
        json body = {
            {"account_id", 103},
            {"amount", 300.0}
        };
        auto res = client.Post("/api/v1/transactions/deposit", body.dump(), "application/json");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["status"] == "COMMITTED");

        auto b103 = adminDb.getAccountBalance(103).value_or(0.0);
        assert(b103 == 7300.0);
    });

    runTest("[API Withdraw] POST /api/v1/transactions/withdraw debits account", [&]() {
        json body = {
            {"account_id", 103},
            {"amount", 200.0}
        };
        auto res = client.Post("/api/v1/transactions/withdraw", body.dump(), "application/json");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["status"] == "COMMITTED");

        auto b103 = adminDb.getAccountBalance(103).value_or(0.0);
        assert(b103 == 7100.0);
    });

    runTest("[API Transactions] GET /api/v1/transactions lists transaction history", [&]() {
        auto res = client.Get("/api/v1/transactions");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["transactions"].is_array());
        assert(j["data"]["count"] >= 4);
    });

    runTest("[API Transactions] GET /api/v1/transactions/:id retrieves single transaction", [&]() {
        auto listRes = client.Get("/api/v1/transactions?limit=1");
        assert(listRes != nullptr && listRes->status == 200);
        auto listJ = json::parse(listRes->body);
        int txId = listJ["data"]["transactions"][0]["transaction_id"];

        auto res = client.Get(("/api/v1/transactions/" + std::to_string(txId)).c_str());
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["transaction_id"] == txId);
    });

    runTest("[API Transactions] GET /api/v1/transactions/:id nonexistent returns 404", [&]() {
        auto res = client.Get("/api/v1/transactions/999999");
        assert(res != nullptr && res->status == 404);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "TRANSACTION_NOT_FOUND");
    });

    // ==========================================
    // 4. Concurrency Monitoring Tests
    // ==========================================

    runTest("[API Concurrency] GET /api/v1/concurrency/status returns pool metrics", [&]() {
        auto res = client.Get("/api/v1/concurrency/status");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"].contains("active_workers"));
        assert(j["data"].contains("pool_size"));
    });

    runTest("[API Concurrency] GET /api/v1/concurrency/transactions returns worker execution list", [&]() {
        auto res = client.Get("/api/v1/concurrency/transactions");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["workers"].is_array());
    });

    // ==========================================
    // 5. Locking and Deadlock Tests
    // ==========================================

    runTest("[API Locks] GET /api/v1/locks returns Lock Table snapshot", [&]() {
        auto res = client.Get("/api/v1/locks");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["locks"].is_array());
        assert(j["data"].contains("total_locks"));
        assert(j["data"].contains("active_transactions"));
    });

    runTest("[API Locks] GET /api/v1/locks/wait-for-graph returns graph vertices & edges", [&]() {
        auto res = client.Get("/api/v1/locks/wait-for-graph");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["vertices"].is_array());
        assert(j["data"]["edges"].is_array());
        assert(j["data"]["cycles"].is_array());
        assert(j["data"]["deadlock_detected"].is_boolean());
    });

    runTest("[API Deadlocks] GET /api/v1/deadlocks evaluates current state without fabricating data", [&]() {
        auto res = client.Get("/api/v1/deadlocks");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["deadlock_present"].is_boolean());
        assert(j["data"]["cycle_count"] == 0);
    });

    runTest("[API Recovery] GET /api/v1/recovery/history returns recorded recovery events", [&]() {
        auto res = client.Get("/api/v1/recovery/history");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["history"].is_array());
    });

    // ==========================================
    // 6. Resource Management & Paging Tests
    // ==========================================

    runTest("[API Resources] GET /api/v1/resources returns tracked logical resources", [&]() {
        auto res = client.Get("/api/v1/resources");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["resources"].is_array());
        assert(j["data"]["resource_count"] >= 4);
    });

    runTest("[API Paging] GET /api/v1/paging/stats returns LRU metrics", [&]() {
        auto res = client.Get("/api/v1/paging/stats");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["is_simulated_os_concept"] == true);
        assert(j["data"].contains("page_faults"));
        assert(j["data"].contains("page_hits"));
        assert(j["data"].contains("hit_ratio"));
    });

    runTest("[API Paging] GET /api/v1/paging/frames returns frame table mappings", [&]() {
        auto res = client.Get("/api/v1/paging/frames");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["frames"].is_array());
        assert(j["data"]["total_frames"] == 64);
    });

    // ==========================================
    // 7. Security, Robustness & CORS Tests
    // ==========================================

    runTest("[API CORS] OPTIONS preflight returns 204 with CORS headers", [&]() {
        auto res = client.Options("/api/v1/transactions/transfer");
        assert(res != nullptr && res->status == 204);
        assert(res->has_header("Access-Control-Allow-Origin"));
        assert(res->has_header("Access-Control-Allow-Methods"));
    });

    runTest("[API Robustness] Unknown endpoint returns JSON 404 envelope", [&]() {
        auto res = client.Get("/api/v1/unknown-endpoint-404");
        assert(res != nullptr && res->status == 404);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "NOT_FOUND");
    });

    runTest("[API Concurrency] Concurrent HTTP transfer requests maintain balance integrity", [&]() {
        // Send 10 concurrent HTTP transfers of ₹10 from 101 to 102
        std::atomic<int> successCount{0};
        std::atomic<int> completedCount{0};
        const int NUM_THREADS = 10;
        std::vector<std::thread> threads;

        for (int i = 0; i < NUM_THREADS; ++i) {
            threads.emplace_back([&]() {
                httplib::Client threadClient(TEST_HOST, TEST_PORT);
                threadClient.set_connection_timeout(std::chrono::seconds(5));
                threadClient.set_read_timeout(std::chrono::seconds(5));
                json payload = {
                    {"from_account", 101},
                    {"to_account", 102},
                    {"amount", 10.0}
                };
                auto r = threadClient.Post("/api/v1/transactions/transfer", payload.dump(), "application/json");
                if (r && r->status == 200) {
                    successCount++;
                }
                completedCount++;
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        assert(completedCount == NUM_THREADS);
        assert(successCount == NUM_THREADS);

        // Verify total balance conservation: sum of 101 and 102 must be strictly conserved
        auto b101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto b102 = adminDb.getAccountBalance(102).value_or(0.0);
        // Before concurrent test: 101 was 9400, 102 was 5600 -> sum = 15000
        assert((b101 + b102) == 15000.0);
    });

    runTest("[API Shutdown] Server stop terminates background thread cleanly", [&]() {
        server.stop();
        assert(!server.isRunning() && "Server must be stopped");
        // Verify client fails to connect once server is stopped
        httplib::Client deadClient(TEST_HOST, TEST_PORT);
        deadClient.set_connection_timeout(std::chrono::milliseconds(200));
        auto r = deadClient.Get("/api/v1/health");
        assert(r == nullptr && "Request to stopped server must fail gracefully");
    });

    std::cout << "\n=======================================================\n";
    std::cout << " [ALL " << g_passedTests << " / " << g_totalTests << " API TESTS PASSED SUCCESSFULLY!]\n";
    std::cout << "=======================================================\n\n";

    return 0;
}
