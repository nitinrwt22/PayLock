#ifndef PAYLOCK_RESOURCE_MANAGER_H
#define PAYLOCK_RESOURCE_MANAGER_H

#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>

namespace paylock {

/**
 * @brief Manages system and transaction resources, memory allocation,
 * and handles foundations for OS Banker's Algorithm and Paging simulation.
 * 
 * OS Concepts Demonstrated:
 * - Resource Allocation Graphs (RAG)
 * - Safe vs. Unsafe states
 * - Buffer pool / memory paging abstractions
 */
class ResourceManager {
public:
    explicit ResourceManager(size_t totalMemoryPages = 64);

    bool requestMemoryPage(int transactionId, int pageId);
    void releaseMemoryPages(int transactionId);

    size_t getAvailablePages() const;
    size_t getTotalPages() const;

    // Simulation hooks for OS paging demonstration
    void simulatePageAccess(int transactionId, int virtualAddress);

private:
    mutable std::mutex resourceMutex_;
    size_t totalPages_;
    std::unordered_map<int, int> pageTable_; // pageId -> holdingTxId
};

} // namespace paylock

#endif // PAYLOCK_RESOURCE_MANAGER_H
