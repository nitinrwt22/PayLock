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
        db.executeQuery("DELETE FROM idempotency_records;");
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

    // ==========================================
    // 8. Task 4 — Persistent Idempotency Tests
    // ==========================================

    runTest("[Idempotency] Missing key allows normal payment (backward compatibility)", [&]() {
        auto initialB101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto initialB102 = adminDb.getAccountBalance(102).value_or(0.0);
        json payload = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", 100.0}
        };
        auto res = client.Post("/api/v1/transactions/transfer", payload.dump(), "application/json");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(!res->has_header("Idempotent-Replayed"));
        assert(adminDb.getAccountBalance(101).value_or(0.0) == initialB101 - 100.0);
        assert(adminDb.getAccountBalance(102).value_or(0.0) == initialB102 + 100.0);
    });

    runTest("[Idempotency] Invalid Idempotency-Key headers rejected with HTTP 400", [&]() {
        json payload = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", 50.0}
        };
        // 1. Whitespace in key
        httplib::Headers headers1 = {{"Idempotency-Key", "invalid key with spaces"}};
        auto res1 = client.Post("/api/v1/transactions/transfer", headers1, payload.dump(), "application/json");
        assert(res1 != nullptr && res1->status == 400);
        auto j1 = json::parse(res1->body);
        assert(j1["error"]["code"] == "INVALID_IDEMPOTENCY_KEY");

        // 2. Empty key
        httplib::Headers headers2 = {{"Idempotency-Key", ""}};
        auto res2 = client.Post("/api/v1/transactions/transfer", headers2, payload.dump(), "application/json");
        assert(res2 != nullptr && res2->status == 400);
        auto j2 = json::parse(res2->body);
        assert(j2["error"]["code"] == "INVALID_IDEMPOTENCY_KEY");

        // 3. Oversized key (>128 chars)
        std::string longKey(135, 'k');
        httplib::Headers headers3 = {{"Idempotency-Key", longKey}};
        auto res3 = client.Post("/api/v1/transactions/transfer", headers3, payload.dump(), "application/json");
        assert(res3 != nullptr && res3->status == 400);
        auto j3 = json::parse(res3->body);
        assert(j3["error"]["code"] == "INVALID_IDEMPOTENCY_KEY");
    });

    int originalTxId = 0;

    runTest("[Idempotency] First request with new key executes successfully (Scenario 1)", [&]() {
        auto initialB101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto initialB102 = adminDb.getAccountBalance(102).value_or(0.0);
        json payload = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", 300.0}
        };
        httplib::Headers headers = {{"Idempotency-Key", "transfer-key-001"}};
        auto res = client.Post("/api/v1/transactions/transfer", headers, payload.dump(), "application/json");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(j["data"]["status"] == "COMMITTED");
        originalTxId = j["data"]["transaction_id"].get<int>();
        assert(originalTxId > 0);

        // Verify balances updated in DB
        assert(adminDb.getAccountBalance(101).value_or(0.0) == initialB101 - 300.0);
        assert(adminDb.getAccountBalance(102).value_or(0.0) == initialB102 + 300.0);

        // Verify stored in idempotency_records table
        auto recOpt = adminDb.getIdempotencyRecord("transfer-key-001");
        assert(recOpt.has_value());
        assert(recOpt->status == "COMMITTED");
        assert(recOpt->responseCode == 200);
        assert(recOpt->transactionId.has_value() && recOpt->transactionId.value() == originalTxId);
    });

    runTest("[Idempotency] Repeating identical key and payload replays stored outcome without re-execution (Scenario 2)", [&]() {
        auto initialB101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto initialB102 = adminDb.getAccountBalance(102).value_or(0.0);
        size_t initialTxCount = adminDb.listTransactions().size();

        json payload = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", 300.0}
        };
        httplib::Headers headers = {{"Idempotency-Key", "transfer-key-001"}};
        auto res = client.Post("/api/v1/transactions/transfer", headers, payload.dump(), "application/json");
        assert(res != nullptr && res->status == 200);
        assert(res->has_header("Idempotent-Replayed"));
        assert(res->get_header_value("Idempotent-Replayed") == "true");

        // Verify response body matches original
        auto j = json::parse(res->body);
        assert(j["data"]["transaction_id"] == originalTxId);
        assert(j["data"]["amount"] == 300.0);

        // CRITICAL: Balances must NOT have changed again!
        assert(adminDb.getAccountBalance(101).value_or(0.0) == initialB101);
        assert(adminDb.getAccountBalance(102).value_or(0.0) == initialB102);

        // CRITICAL: Transaction count must NOT have increased!
        assert(adminDb.listTransactions().size() == initialTxCount);
    });

    runTest("[Idempotency] Reusing key with different amount returns HTTP 409 conflict (Scenario 3)", [&]() {
        auto initialB101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto initialB102 = adminDb.getAccountBalance(102).value_or(0.0);
        size_t initialTxCount = adminDb.listTransactions().size();

        json payload = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", 777.0} // Different amount
        };
        httplib::Headers headers = {{"Idempotency-Key", "transfer-key-001"}};
        auto res = client.Post("/api/v1/transactions/transfer", headers, payload.dump(), "application/json");
        assert(res != nullptr && res->status == 409);
        auto j = json::parse(res->body);
        assert(j["success"] == false);
        assert(j["error"]["code"] == "IDEMPOTENCY_CONFLICT");

        // Balances and tx count untouched
        assert(adminDb.getAccountBalance(101).value_or(0.0) == initialB101);
        assert(adminDb.getAccountBalance(102).value_or(0.0) == initialB102);
        assert(adminDb.listTransactions().size() == initialTxCount);
    });

    runTest("[Idempotency] Reusing key for different operation is rejected with HTTP 409 (Scenario 4)", [&]() {
        auto initialB101 = adminDb.getAccountBalance(101).value_or(0.0);
        size_t initialTxCount = adminDb.listTransactions().size();

        // Attempt deposit with transfer-key-001
        json depPayload = {
            {"account_id", 101},
            {"amount", 300.0}
        };
        httplib::Headers headers = {{"Idempotency-Key", "transfer-key-001"}};
        auto resDep = client.Post("/api/v1/transactions/deposit", headers, depPayload.dump(), "application/json");
        assert(resDep != nullptr && resDep->status == 409);
        auto jDep = json::parse(resDep->body);
        assert(jDep["error"]["code"] == "IDEMPOTENCY_CONFLICT");

        // Attempt withdraw with transfer-key-001
        json withPayload = {
            {"account_id", 101},
            {"amount", 300.0}
        };
        auto resWith = client.Post("/api/v1/transactions/withdraw", headers, withPayload.dump(), "application/json");
        assert(resWith != nullptr && resWith->status == 409);
        auto jWith = json::parse(resWith->body);
        assert(jWith["error"]["code"] == "IDEMPOTENCY_CONFLICT");

        assert(adminDb.getAccountBalance(101).value_or(0.0) == initialB101);
        assert(adminDb.listTransactions().size() == initialTxCount);
    });

    runTest("[Idempotency Concurrency] Concurrent requests with same key do not double-process (Scenario 5)", [&]() {
        auto initialB101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto initialB102 = adminDb.getAccountBalance(102).value_or(0.0);
        size_t initialTxCount = adminDb.listTransactions().size();

        const std::string concurrentKey = "concurrent-transfer-key-999";
        const double transferAmount = 150.0;
        json payload = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", transferAmount}
        };

        const int NUM_CONCURRENT = 4;
        std::vector<std::thread> threads;
        std::vector<int> statusCodes(NUM_CONCURRENT, 0);

        for (int i = 0; i < NUM_CONCURRENT; ++i) {
            threads.emplace_back([&, i]() {
                httplib::Client threadClient(TEST_HOST, TEST_PORT);
                threadClient.set_connection_timeout(std::chrono::seconds(5));
                threadClient.set_read_timeout(std::chrono::seconds(5));
                httplib::Headers h = {{"Idempotency-Key", concurrentKey}};
                auto r = threadClient.Post("/api/v1/transactions/transfer", h, payload.dump(), "application/json");
                if (r) {
                    statusCodes[i] = r->status;
                }
            });
        }

        for (auto& t : threads) {
            t.join();
        }

        // At least one thread must be HTTP 200
        int count200 = 0;
        for (int i = 0; i < NUM_CONCURRENT; ++i) {
            if (statusCodes[i] == 200) count200++;
        }
        assert(count200 >= 1 && "At least one request must succeed");

        // CRITICAL CONCURRENCY INVARIANT:
        // Account 101 must be debited EXACTLY ONCE for transferAmount (₹150.0)
        auto finalB101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto finalB102 = adminDb.getAccountBalance(102).value_or(0.0);
        assert(finalB101 == (initialB101 - transferAmount));
        assert(finalB102 == (initialB102 + transferAmount));

        // CRITICAL CONCURRENCY INVARIANT:
        // Total transactions in MySQL must have increased by EXACTLY 1 (NO double execution)
        size_t finalTxCount = adminDb.listTransactions().size();
        assert(finalTxCount == (initialTxCount + 1));
    });

    runTest("[Idempotency] Failed transaction rolls back and does not leave false success (Scenario 6)", [&]() {
        auto initialB101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto initialB102 = adminDb.getAccountBalance(102).value_or(0.0);

        // Attempt transfer of ₹999999 (exceeding balance)
        json payload = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", 999999.0}
        };
        httplib::Headers headers = {{"Idempotency-Key", "overdraft-key-006"}};
        auto res = client.Post("/api/v1/transactions/transfer", headers, payload.dump(), "application/json");
        assert(res != nullptr && res->status == 409);
        auto j = json::parse(res->body);
        assert(j["success"] == false);

        // Verify idempotency record in DB is FAILED, not COMMITTED
        auto recOpt = adminDb.getIdempotencyRecord("overdraft-key-006");
        assert(recOpt.has_value());
        assert(recOpt->status == "FAILED");
        assert(recOpt->responseCode == 409);

        // Repeating request with same key replays failure and does NOT succeed
        auto res2 = client.Post("/api/v1/transactions/transfer", headers, payload.dump(), "application/json");
        assert(res2 != nullptr && res2->status == 409);
        assert(res2->has_header("Idempotent-Replayed"));
        auto j2 = json::parse(res2->body);
        assert(j2["success"] == false);

        // Balances remain strictly untouched
        assert(adminDb.getAccountBalance(101).value_or(0.0) == initialB101);
        assert(adminDb.getAccountBalance(102).value_or(0.0) == initialB102);
    });

    runTest("[Idempotency] Different key executes a separate legitimate payment (Scenario 7)", [&]() {
        auto initialB101 = adminDb.getAccountBalance(101).value_or(0.0);
        auto initialB102 = adminDb.getAccountBalance(102).value_or(0.0);
        size_t initialTxCount = adminDb.listTransactions().size();

        json payload = {
            {"from_account", 101},
            {"to_account", 102},
            {"amount", 75.0}
        };
        httplib::Headers headers = {{"Idempotency-Key", "legit-new-key-007"}};
        auto res = client.Post("/api/v1/transactions/transfer", headers, payload.dump(), "application/json");
        assert(res != nullptr && res->status == 200);
        auto j = json::parse(res->body);
        assert(j["success"] == true);
        assert(!res->has_header("Idempotent-Replayed"));

        // Balances updated
        assert(adminDb.getAccountBalance(101).value_or(0.0) == initialB101 - 75.0);
        assert(adminDb.getAccountBalance(102).value_or(0.0) == initialB102 + 75.0);
        assert(adminDb.listTransactions().size() == initialTxCount + 1);
    });

    runTest("[Idempotency Deposit & Withdraw] Deposit and withdraw endpoints support persistent idempotency", [&]() {
        // --- DEPOSIT ---
        auto initialB103 = adminDb.getAccountBalance(103).value_or(0.0);
        json depPayload = {{"account_id", 103}, {"amount", 500.0}};
        httplib::Headers depHeaders = {{"Idempotency-Key", "deposit-key-100"}};

        // 1. First deposit executes
        auto depRes1 = client.Post("/api/v1/transactions/deposit", depHeaders, depPayload.dump(), "application/json");
        assert(depRes1 != nullptr && depRes1->status == 200);
        assert(adminDb.getAccountBalance(103).value_or(0.0) == initialB103 + 500.0);

        // 2. Repeat deposit replays without double-credit
        auto depRes2 = client.Post("/api/v1/transactions/deposit", depHeaders, depPayload.dump(), "application/json");
        assert(depRes2 != nullptr && depRes2->status == 200);
        assert(depRes2->has_header("Idempotent-Replayed"));
        assert(adminDb.getAccountBalance(103).value_or(0.0) == initialB103 + 500.0);

        // 3. Deposit conflict with different amount
        json depConflict = {{"account_id", 103}, {"amount", 999.0}};
        auto depRes3 = client.Post("/api/v1/transactions/deposit", depHeaders, depConflict.dump(), "application/json");
        assert(depRes3 != nullptr && depRes3->status == 409);

        // --- WITHDRAW ---
        json withPayload = {{"account_id", 103}, {"amount", 200.0}};
        httplib::Headers withHeaders = {{"Idempotency-Key", "withdraw-key-200"}};

        // 1. First withdraw executes
        auto withRes1 = client.Post("/api/v1/transactions/withdraw", withHeaders, withPayload.dump(), "application/json");
        assert(withRes1 != nullptr && withRes1->status == 200);
        assert(adminDb.getAccountBalance(103).value_or(0.0) == initialB103 + 500.0 - 200.0);

        // 2. Repeat withdraw replays without double-debit
        auto withRes2 = client.Post("/api/v1/transactions/withdraw", withHeaders, withPayload.dump(), "application/json");
        assert(withRes2 != nullptr && withRes2->status == 200);
        assert(withRes2->has_header("Idempotent-Replayed"));
        assert(adminDb.getAccountBalance(103).value_or(0.0) == initialB103 + 500.0 - 200.0);

        // 3. Withdraw conflict with different amount
        json withConflict = {{"account_id", 103}, {"amount", 999.0}};
        auto withRes3 = client.Post("/api/v1/transactions/withdraw", withHeaders, withConflict.dump(), "application/json");
        assert(withRes3 != nullptr && withRes3->status == 409);
    });

    runTest("[Idempotency Recovery] Server startup cleans up dangling PROCESSING records", [&]() {
        // Insert a simulated stranded in-flight record
        adminDb.executeQuery("INSERT INTO idempotency_records "
                             "(idempotency_key, operation_type, request_hash, request_payload, status, created_at) "
                             "VALUES ('stranded-crash-key', 'TRANSFER', 'fakehash', 'payload', 'PROCESSING', NOW());");

        // Trigger restart cleanup
        adminDb.cleanupOrphanedProcessingRecords();

        auto recOpt = adminDb.getIdempotencyRecord("stranded-crash-key");
        assert(recOpt.has_value());
        assert(recOpt->status == "FAILED");
        assert(recOpt->responseCode == 500);
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
