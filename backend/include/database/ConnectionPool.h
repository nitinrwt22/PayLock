#ifndef PAYLOCK_CONNECTION_POOL_H
#define PAYLOCK_CONNECTION_POOL_H

#include "database/DatabaseManager.h"
#include <memory>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <chrono>
#include <atomic>
#include <stdexcept>
#include <string>

namespace paylock {

class ConnectionPool;

/**
 * @brief Thread-safe RAII lease for a pooled DatabaseManager connection.
 * Automatically validates, resets session state, and returns connection to pool on destruction.
 */
class PooledConnection {
public:
    PooledConnection() noexcept = default;

    PooledConnection(ConnectionPool* pool, std::unique_ptr<DatabaseManager> conn) noexcept
        : pool_(pool), conn_(std::move(conn)) {}

    ~PooledConnection();

    // Move semantics
    PooledConnection(PooledConnection&& other) noexcept;
    PooledConnection& operator=(PooledConnection&& other) noexcept;

    // Non-copyable to prevent duplicate concurrent lease
    PooledConnection(const PooledConnection&) = delete;
    PooledConnection& operator=(const PooledConnection&) = delete;

    DatabaseManager* get() const noexcept { return conn_.get(); }
    DatabaseManager* operator->() const noexcept { return conn_.get(); }
    DatabaseManager& operator*() const noexcept { return *conn_; }
    bool isValid() const noexcept { return conn_ != nullptr; }

    void reset() noexcept;

private:
    ConnectionPool* pool_{nullptr};
    std::unique_ptr<DatabaseManager> conn_{nullptr};
};

/**
 * @brief Observability metrics for the MySQL Connection Pool.
 */
struct PoolMetrics {
    size_t maxCapacity{0};
    size_t totalCreated{0};
    size_t inUse{0};
    size_t available{0};
    size_t waitingRequests{0};
    size_t acquisitionFailures{0};
    size_t totalAcquisitions{0};
};

/**
 * @brief Thread-safe MySQL Connection Pool for PayLock.
 * 
 * OS & DBMS Principles:
 * - Thread-safe connection leasing via standard C++ synchronization (std::mutex, std::condition_variable)
 * - Exclusive borrowing (a connection is never shared between concurrent requests)
 * - Scope-safe RAII reclamation (PooledConnection)
 * - Transaction safety & session isolation (automatic ROLLBACK and mysql_reset_connection on return)
 * - Overload protection with configurable wait timeouts (no busy waiting)
 * - Liveness verification & graceful broken connection recovery
 * - Rich observability metrics without exposing sensitive database credentials
 */
class ConnectionPool : public std::enable_shared_from_this<ConnectionPool> {
public:
    explicit ConnectionPool(DatabaseConfig config, size_t initialConnections = 0);
    ~ConnectionPool();

    ConnectionPool(const ConnectionPool&) = delete;
    ConnectionPool& operator=(const ConnectionPool&) = delete;

    /**
     * @brief Acquires an exclusive connection lease from the pool.
     * Blocks if pool capacity is reached until a connection is returned or timeout expires.
     * 
     * @param timeout Max duration to wait. Defaults to config_.poolTimeoutMs if 0ms.
     * @return PooledConnection RAII handle.
     * @throws DatabaseException if timeout expires or connection creation fails.
     */
    PooledConnection acquire(std::chrono::milliseconds timeout = std::chrono::milliseconds(0));

    /**
     * @brief Releases a connection back to the pool after verifying and resetting session state.
     * Called automatically by ~PooledConnection().
     */
    void release(std::unique_ptr<DatabaseManager> conn) noexcept;

    /**
     * @brief Shuts down the pool, closing all idle connections and notifying waiting borrowers.
     */
    void shutdown() noexcept;

    /**
     * @brief Snapshot of real-time pool metrics.
     */
    PoolMetrics getMetrics() const noexcept;

    /**
     * @brief Checks if pool is active and operational.
     */
    bool isHealthy() const noexcept;

    size_t getCapacity() const noexcept { return maxPoolSize_; }
    const DatabaseConfig& getConfig() const noexcept { return config_; }

private:
    struct PooledEntry {
        std::unique_ptr<DatabaseManager> conn;
        std::chrono::steady_clock::time_point lastActive;
    };

    DatabaseConfig config_;
    const size_t maxPoolSize_;
    const std::chrono::milliseconds defaultTimeout_;

    std::vector<PooledEntry> availableConnections_;
    size_t totalCreated_{0};
    size_t inUseCount_{0};
    size_t waitingRequests_{0};
    size_t acquisitionFailures_{0};
    size_t totalAcquisitions_{0};

    std::atomic<bool> isShuttingDown_{false};
    mutable std::mutex poolMutex_;
    std::condition_variable cvAvailable_;
};

} // namespace paylock

#endif // PAYLOCK_CONNECTION_POOL_H
