#include "database/ConnectionPool.h"
#include "logging/Logger.h"

namespace paylock {

// ============================================================================
// PooledConnection RAII Lease Implementation
// ============================================================================

PooledConnection::~PooledConnection() {
    reset();
}

PooledConnection::PooledConnection(PooledConnection&& other) noexcept
    : pool_(other.pool_), conn_(std::move(other.conn_)) {
    other.pool_ = nullptr;
}

PooledConnection& PooledConnection::operator=(PooledConnection&& other) noexcept {
    if (this != &other) {
        reset();
        pool_ = other.pool_;
        conn_ = std::move(other.conn_);
        other.pool_ = nullptr;
    }
    return *this;
}

void PooledConnection::reset() noexcept {
    if (pool_ && conn_) {
        pool_->release(std::move(conn_));
        pool_ = nullptr;
    }
}

// ============================================================================
// ConnectionPool Implementation
// ============================================================================

ConnectionPool::ConnectionPool(DatabaseConfig config, size_t initialConnections)
    : config_(std::move(config)),
      maxPoolSize_(config_.poolSize > 0 ? config_.poolSize : 10),
      defaultTimeout_(std::chrono::milliseconds(config_.poolTimeoutMs > 0 ? config_.poolTimeoutMs : 3000)) {
    
    Logger::info("Initializing MySQL Connection Pool [Max Size: " + std::to_string(maxPoolSize_)
                 + ", Default Timeout: " + std::to_string(defaultTimeout_.count()) + "ms]", "ConnectionPool");

    size_t toPrewarm = std::min(initialConnections, maxPoolSize_);
    for (size_t i = 0; i < toPrewarm; ++i) {
        try {
            auto conn = std::make_unique<DatabaseManager>(config_);
            conn->connect();
            availableConnections_.push_back({std::move(conn), std::chrono::steady_clock::now()});
            totalCreated_++;
        } catch (const std::exception& ex) {
            Logger::warn("Pre-warming connection #" + std::to_string(i + 1) + " failed: " + ex.what()
                         + " (pool will continue lazily)", "ConnectionPool");
            break;
        }
    }

    if (totalCreated_ > 0) {
        Logger::info("Connection pool pre-warmed with " + std::to_string(totalCreated_) + " live MySQL connection(s)", "ConnectionPool");
    }
}

ConnectionPool::~ConnectionPool() {
    shutdown();
}

PooledConnection ConnectionPool::acquire(std::chrono::milliseconds timeout) {
    if (timeout.count() <= 0) {
        timeout = defaultTimeout_;
    }

    std::unique_lock<std::mutex> lock(poolMutex_);

    if (isShuttingDown_.load()) {
        throw DatabaseException("Cannot acquire connection: MySQL Connection Pool is shutting down.");
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;
    waitingRequests_++;

    struct WaitingGuard {
        size_t& count;
        ~WaitingGuard() { count--; }
    } waitGuard{waitingRequests_};

    // Block safely until a connection is available or capacity exists to create one
    while (availableConnections_.empty() && totalCreated_ >= maxPoolSize_) {
        if (isShuttingDown_.load()) {
            throw DatabaseException("Cannot acquire connection: MySQL Connection Pool is shutting down.");
        }

        if (cvAvailable_.wait_until(lock, deadline) == std::cv_status::timeout) {
            acquisitionFailures_++;
            throw DatabaseException("Connection pool exhausted: timed out waiting for an available connection ("
                                    + std::to_string(timeout.count()) + "ms timeout exceeded, active="
                                    + std::to_string(inUseCount_) + "/" + std::to_string(maxPoolSize_) + ").");
        }
    }

    if (isShuttingDown_.load()) {
        throw DatabaseException("Cannot acquire connection: MySQL Connection Pool is shutting down.");
    }

    // 1. Re-use an existing idle connection if available
    if (!availableConnections_.empty()) {
        auto entry = std::move(availableConnections_.back());
        availableConnections_.pop_back();
        auto conn = std::move(entry.conn);
        auto lastActive = entry.lastActive;
        inUseCount_++;
        totalAcquisitions_++;

        lock.unlock(); // Release pool mutex during ping check if needed

        // Only ping if connection has been idle for more than 1000ms
        auto now = std::chrono::steady_clock::now();
        if (now - lastActive > std::chrono::milliseconds(1000)) {
            if (!conn->ping()) {
                Logger::warn("Pooled MySQL connection failed idle ping check; attempting reconnection", "ConnectionPool");
                try {
                    conn->connect();
                } catch (const std::exception& ex) {
                    lock.lock();
                    totalCreated_--;
                    inUseCount_--;
                    acquisitionFailures_++;
                    cvAvailable_.notify_one();
                    throw;
                }
            }
        }

        return PooledConnection(this, std::move(conn));
    }

    // 2. Allocate a new connection within maxPoolSize_
    totalCreated_++;
    inUseCount_++;
    totalAcquisitions_++;

    lock.unlock(); // Release pool mutex while opening TCP socket & doing MySQL handshake

    try {
        auto newConn = std::make_unique<DatabaseManager>(config_);
        newConn->connect();
        return PooledConnection(this, std::move(newConn));
    } catch (const std::exception& ex) {
        lock.lock();
        totalCreated_--;
        inUseCount_--;
        acquisitionFailures_++;
        cvAvailable_.notify_one();
        throw;
    }
}

void ConnectionPool::release(std::unique_ptr<DatabaseManager> conn) noexcept {
    if (!conn) return;

    // Step 3: Transaction Correctness & Session Reset
    // Performed outside poolMutex_ so MySQL queries (ROLLBACK, RESET) do not block other borrowers
    bool healthy = false;
    try {
        healthy = conn->resetSession();
    } catch (...) {
        healthy = false;
    }

    std::lock_guard<std::mutex> lock(poolMutex_);
    inUseCount_--;

    if (!healthy || isShuttingDown_.load()) {
        try {
            conn->disconnect();
        } catch (...) {}
        totalCreated_--;
        cvAvailable_.notify_one();
        return;
    }

    availableConnections_.push_back({std::move(conn), std::chrono::steady_clock::now()});
    cvAvailable_.notify_one();
}

void ConnectionPool::shutdown() noexcept {
    bool expected = false;
    if (!isShuttingDown_.compare_exchange_strong(expected, true)) {
        return;
    }

    Logger::info("Shutting down MySQL Connection Pool...", "ConnectionPool");

    std::lock_guard<std::mutex> lock(poolMutex_);
    for (auto& entry : availableConnections_) {
        if (entry.conn) {
            try { entry.conn->disconnect(); } catch (...) {}
        }
    }
    availableConnections_.clear();
    totalCreated_ = inUseCount_;
    cvAvailable_.notify_all();
}

PoolMetrics ConnectionPool::getMetrics() const noexcept {
    std::lock_guard<std::mutex> lock(poolMutex_);
    PoolMetrics m;
    m.maxCapacity = maxPoolSize_;
    m.totalCreated = totalCreated_;
    m.inUse = inUseCount_;
    m.available = availableConnections_.size();
    m.waitingRequests = waitingRequests_;
    m.acquisitionFailures = acquisitionFailures_;
    m.totalAcquisitions = totalAcquisitions_;
    return m;
}

bool ConnectionPool::isHealthy() const noexcept {
    if (isShuttingDown_.load()) return false;
    std::lock_guard<std::mutex> lock(poolMutex_);
    return totalCreated_ > 0 || inUseCount_ < maxPoolSize_;
}

} // namespace paylock
