#ifndef PAYLOCK_HTTP_SERVER_H
#define PAYLOCK_HTTP_SERVER_H

#include <string>
#include <memory>
#include <thread>
#include <atomic>
#include <httplib.h>
#include "database/ConnectionPool.h"
#include "api/ApiController.h"

namespace paylock {
namespace api {

/**
 * @brief Embedded HTTP REST API Server for PayLock.
 * Exposes core backend services to web dashboards and HTTP clients.
 */
class HttpServer {
public:
    HttpServer(DatabaseConfig dbConfig,
               DatabaseManager& adminDb,
               LockManager& lockManager,
               RecoveryManager& recoveryManager,
               ResourceManager& resourceManager,
               PagingSimulator& pagingSimulator,
               ConcurrencyManager* concurrencyManager = nullptr,
               std::string host = "127.0.0.1",
               int port = 8080,
               std::shared_ptr<ConnectionPool> pool = nullptr);

    ~HttpServer();

    // Prevent copying
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /**
     * @brief Starts the HTTP server.
     * @param async If true, runs server on a dedicated background thread.
     * @return true if successfully bound and listening, false otherwise.
     */
    bool start(bool async = true);

    /**
     * @brief Stops the HTTP server and joins worker thread.
     */
    void stop();

    bool isRunning() const;
    std::string getHost() const { return host_; }
    int getPort() const { return port_; }
    std::string getBaseUrl() const;
    std::shared_ptr<ConnectionPool> getConnectionPool() const noexcept { return pool_; }

private:
    std::string host_;
    int port_;
    std::atomic<bool> isRunning_{false};

    std::shared_ptr<ConnectionPool> pool_;
    httplib::Server server_;
    std::unique_ptr<ApiController> controller_;
    std::thread serverThread_;

    void setupMiddleware();
};

} // namespace api
} // namespace paylock

#endif // PAYLOCK_HTTP_SERVER_H
