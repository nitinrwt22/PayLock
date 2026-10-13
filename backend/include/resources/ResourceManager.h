#ifndef PAYLOCK_RESOURCE_MANAGER_H
#define PAYLOCK_RESOURCE_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <optional>

namespace paylock {

/**
 * @brief Logical resource types managed by PayLock.
 * Distinct from LockManager application/database-style locks.
 */
enum class ResourceType {
    ACCOUNT_RECORD,     // Logical cache/buffer slots for account records
    TRANSACTION_SLOT,   // Maximum concurrent transaction execution slots
    WORKER_CAPACITY,    // Available worker thread processing units
    MEMORY_FRAME,       // Simulated physical memory frames
    CUSTOM              // Custom user-defined resource
};

/**
 * @brief Result status of a resource allocation attempt.
 */
enum class ResourceAllocationResult {
    GRANTED,            // Resource allocated successfully
    EXHAUSTED,          // Insufficient capacity currently available
    INVALID_REQUEST     // Invalid requested amount or unregistered resource
};

/**
 * @brief Metadata and metrics for a specific logical resource.
 */
struct ResourceEntry {
    std::string resourceId;
    ResourceType type{ResourceType::CUSTOM};
    size_t totalCapacity{0};
    size_t allocatedCapacity{0};

    size_t getAvailableCapacity() const {
        return (totalCapacity >= allocatedCapacity) ? (totalCapacity - allocatedCapacity) : 0;
    }
};

/**
 * @brief Immutable point-in-time snapshot of system resources.
 */
struct SystemResourceSnapshot {
    std::vector<ResourceEntry> resources;
    size_t totalAllocated{0};
    size_t totalAvailable{0};

    std::string toString() const;
};

/**
 * @brief Manages logical system resources, capacity limits, and allocations.
 *
 * OS Concepts Demonstrated:
 * - Resource capacity limits & admission control
 * - Prevention of resource starvation & negative counters
 * - Separation between logical resources and concurrency locks
 * - Thread-safe critical sections for resource allocation
 */
class ResourceManager {
public:
    explicit ResourceManager(size_t totalMemoryPages = 64);

    // --- Logical Resource Management ---

    /**
     * @brief Registers a logical resource with total capacity.
     */
    void registerResource(const std::string& resourceId, ResourceType type, size_t totalCapacity);

    /**
     * @brief Requests allocation of units for a logical resource.
     * @param resourceId Resource identifier
     * @param amount Units to allocate
     * @param ownerId Optional transaction or worker ID claiming ownership
     * @return Result status (GRANTED, EXHAUSTED, INVALID_REQUEST)
     */
    ResourceAllocationResult allocateResource(const std::string& resourceId, size_t amount = 1, int ownerId = 0);

    /**
     * @brief Releases allocated units of a logical resource.
     * @param resourceId Resource identifier
     * @param amount Units to release
     * @param ownerId Optional owner ID
     * @return true if released safely, false if amount exceeded allocated capacity
     */
    bool releaseResource(const std::string& resourceId, size_t amount = 1, int ownerId = 0);

    /**
     * @brief Releases all resources held by a specific owner ID.
     */
    size_t releaseAllForOwner(int ownerId);

    // --- Capacity Inquiries ---
    size_t getTotalCapacity(const std::string& resourceId) const;
    size_t getAllocatedCapacity(const std::string& resourceId) const;
    size_t getAvailableCapacity(const std::string& resourceId) const;
    bool hasResource(const std::string& resourceId) const;

    /**
     * @brief Takes an atomic snapshot of all managed resources.
     */
    SystemResourceSnapshot getSnapshot() const;

    /**
     * @brief Resets all resources to unallocated state.
     */
    void reset();

    // --- Backward Compatible Memory Page Stubs ---
    bool requestMemoryPage(int transactionId, int pageId);
    void releaseMemoryPages(int transactionId);
    size_t getAvailablePages() const;
    size_t getTotalPages() const;
    void simulatePageAccess(int transactionId, int virtualAddress);

    // --- Demonstration Fixture ---
    static void demonstrateResourceManagement();

private:
    mutable std::mutex resourceMutex_;

    // Standardized resources map: resourceId -> ResourceEntry
    std::unordered_map<std::string, ResourceEntry> resourceTable_;

    // Per-owner allocation tracking: (ownerId, resourceId) -> count
    std::unordered_map<int, std::unordered_map<std::string, size_t>> ownerAllocations_;

    // Backward-compatible memory page map: pageId -> holdingTxId
    size_t totalPages_;
    std::unordered_map<int, int> pageTable_;
};

} // namespace paylock

#endif // PAYLOCK_RESOURCE_MANAGER_H
