#include "api/HttpServer.h"
#include "api/ApiResponse.h"
#include "logging/Logger.h"
#include <chrono>

namespace paylock {
namespace api {

HttpServer::HttpServer(DatabaseConfig dbConfig,
                       DatabaseManager& adminDb,
                       LockManager& lockManager,
                       RecoveryManager& recoveryManager,
                       ResourceManager& resourceManager,
                       PagingSimulator& pagingSimulator,
                       ConcurrencyManager* concurrencyManager,
                       std::string host,
                       int port,
                       std::shared_ptr<ConnectionPool> pool)
    : host_(std::move(host)),
      port_(port),
      pool_(pool ? pool : std::make_shared<ConnectionPool>(dbConfig)),
      controller_(std::make_unique<ApiController>(std::move(dbConfig),
                                                  adminDb,
                                                  lockManager,
                                                  recoveryManager,
                                                  resourceManager,
                                                  pagingSimulator,
                                                  concurrencyManager,
                                                  pool_)) {
    setupMiddleware();
    controller_->registerRoutes(server_);
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::setupMiddleware() {
    // 1. CORS Pre-flight Options Handler
    server_.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, Idempotency-Key");
        res.set_header("Access-Control-Expose-Headers", "Idempotent-Replayed");
    });

    // 2. Pre-routing: Telemetry Logging (only log mutating actions like POST/DELETE to keep console clean)
    server_.set_pre_routing_handler([](const httplib::Request& req, httplib::Response&) {
        if (req.method != "GET" && req.method != "OPTIONS") {
            Logger::info("[HTTP " + req.method + "] " + req.path, "HttpServer");
        }
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // 3. Post-routing: Standard CORS Headers
    server_.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization, X-Requested-With, Idempotency-Key");
        res.set_header("Access-Control-Expose-Headers", "Idempotent-Replayed");
    });

    // 4. Custom 404 Handler
    server_.set_error_handler([](const httplib::Request& req, httplib::Response& res) {
        if (res.status == 404 && res.body.empty()) {
            ApiResponse::sendError(res, "NOT_FOUND", "Endpoint '" + req.path + "' not found", 404);
        }
    });
}

bool HttpServer::start(bool async) {
    if (isRunning_.load()) {
        Logger::warn("HttpServer is already running on " + getBaseUrl(), "HttpServer");
        return true;
    }

    Logger::info("Starting PayLock HTTP API server on " + getBaseUrl(), "HttpServer");

    if (async) {
        serverThread_ = std::thread([this]() {
            server_.listen(host_.c_str(), port_);
            isRunning_ = false;
        });

        // Wait for server to bind & start listening
        int attempts = 0;
        while (!server_.is_running() && attempts < 100) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            attempts++;
        }

        if (server_.is_running()) {
            isRunning_ = true;
            Logger::info("PayLock HTTP API server successfully bound and listening at " + getBaseUrl(), "HttpServer");
            return true;
        } else {
            Logger::error("Failed to bind PayLock HTTP API server on " + getBaseUrl(), "HttpServer");
            stop();
            return false;
        }
    } else {
        isRunning_ = true;
        return server_.listen(host_.c_str(), port_);
    }
}

void HttpServer::stop() {
    if (server_.is_running()) {
        Logger::info("Stopping PayLock HTTP API server...", "HttpServer");
        server_.stop();
    }

    if (serverThread_.joinable()) {
        serverThread_.join();
    }

    if (controller_) {
        controller_->shutdownPool();
    }
    if (pool_) {
        pool_->shutdown();
    }

    isRunning_ = false;
}

bool HttpServer::isRunning() const {
    return isRunning_.load() && server_.is_running();
}

std::string HttpServer::getBaseUrl() const {
    return "http://" + host_ + ":" + std::to_string(port_);
}

} // namespace api
} // namespace paylock
