#include "resources/ResourceManager.h"
#include "logging/Logger.h"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace paylock {

std::string SystemResourceSnapshot::toString() const {
    std::ostringstream oss;
    oss << "=================================================================\n";
    oss << "                  PAYLOCK RESOURCE STATUS SNAPSHOT               \n";
    oss << "=================================================================\n";
    oss << std::left << std::setw(22) << "RESOURCE ID"
        << std::setw(14) << "TOTAL"
        << std::setw(14) << "ALLOCATED"
        << std::setw(14) << "AVAILABLE"
        << "UTILIZATION\n";
    oss << "-----------------------------------------------------------------\n";

    for (const auto& res : resources) {
        double util = (res.totalCapacity > 0)
            ? (static_cast<double>(res.allocatedCapacity) / res.totalCapacity * 100.0)
            : 0.0;

        std::ostringstream utilStr;
        utilStr << std::fixed << std::setprecision(1) << util << "%";

        oss << std::left << std::setw(22) << res.resourceId
            << std::setw(14) << res.totalCapacity
            << std::setw(14) << res.allocatedCapacity
            << std::setw(14) << res.getAvailableCapacity()
            << utilStr.str() << "\n";
    }

    oss << "=================================================================\n";
    oss << "Summary: " << totalAllocated << " units allocated, "
        << totalAvailable << " units available.\n";
    oss << "=================================================================\n";
    return oss.str();
}

ResourceManager::ResourceManager(size_t totalMemoryPages)
    : totalPages_(totalMemoryPages) {
    // Register default system resources
    registerResource("ACCOUNT_RECORDS", ResourceType::ACCOUNT_RECORD, 1000);
    registerResource("TRANSACTION_SLOTS", ResourceType::TRANSACTION_SLOT, 64);
    registerResource("WORKER_CAPACITY", ResourceType::WORKER_CAPACITY, 16);
    registerResource("MEMORY_FRAMES", ResourceType::MEMORY_FRAME, totalMemoryPages);
}

void ResourceManager::registerResource(const std::string& resourceId, ResourceType type, size_t totalCapacity) {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    auto& entry = resourceTable_[resourceId];
    entry.resourceId = resourceId;
    entry.type = type;
    entry.totalCapacity = totalCapacity;
    entry.allocatedCapacity = 0;
    Logger::info("Registered logical resource: " + resourceId + " (Capacity: " + std::to_string(totalCapacity) + ")", "ResourceManager");
}

ResourceAllocationResult ResourceManager::allocateResource(const std::string& resourceId, size_t amount, int ownerId) {
    std::lock_guard<std::mutex> lock(resourceMutex_);

    if (amount == 0) {
        Logger::warn("Invalid allocation request: amount must be > 0", "ResourceManager");
        return ResourceAllocationResult::INVALID_REQUEST;
    }

    auto it = resourceTable_.find(resourceId);
    if (it == resourceTable_.end()) {
        Logger::warn("Allocation rejected: Unknown resource '" + resourceId + "'", "ResourceManager");
        return ResourceAllocationResult::INVALID_REQUEST;
    }

    ResourceEntry& entry = it->second;
    if (entry.getAvailableCapacity() < amount) {
        Logger::warn("Resource exhausted: '" + resourceId + "' requested " + std::to_string(amount) + 
                     ", available: " + std::to_string(entry.getAvailableCapacity()), "ResourceManager");
        return ResourceAllocationResult::EXHAUSTED;
    }

    entry.allocatedCapacity += amount;
    if (ownerId != 0) {
        ownerAllocations_[ownerId][resourceId] += amount;
    }

    Logger::info("Allocated " + std::to_string(amount) + " unit(s) of '" + resourceId + 
                 "' to Owner #" + std::to_string(ownerId) + " (Remaining: " + 
                 std::to_string(entry.getAvailableCapacity()) + ")", "ResourceManager");
    return ResourceAllocationResult::GRANTED;
}

bool ResourceManager::releaseResource(const std::string& resourceId, size_t amount, int ownerId) {
    std::lock_guard<std::mutex> lock(resourceMutex_);

    if (amount == 0) {
        Logger::warn("Invalid release request: amount must be > 0", "ResourceManager");
        return false;
    }

    auto it = resourceTable_.find(resourceId);
    if (it == resourceTable_.end()) {
        Logger::warn("Release failed: Unknown resource '" + resourceId + "'", "ResourceManager");
        return false;
    }

    ResourceEntry& entry = it->second;
    if (entry.allocatedCapacity < amount) {
        Logger::warn("Release rejected: Attempted to release " + std::to_string(amount) + 
                     " units from '" + resourceId + "', but only " + 
                     std::to_string(entry.allocatedCapacity) + " are allocated", "ResourceManager");
        return false;
    }

    entry.allocatedCapacity -= amount;

    if (ownerId != 0) {
        auto ownerIt = ownerAllocations_.find(ownerId);
        if (ownerIt != ownerAllocations_.end()) {
            auto resIt = ownerIt->second.find(resourceId);
            if (resIt != ownerIt->second.end()) {
                if (resIt->second <= amount) {
                    ownerIt->second.erase(resIt);
                } else {
                    resIt->second -= amount;
                }
            }
            if (ownerIt->second.empty()) {
                ownerAllocations_.erase(ownerIt);
            }
        }
    }

    Logger::info("Released " + std::to_string(amount) + " unit(s) of '" + resourceId + 
                 "' by Owner #" + std::to_string(ownerId) + " (Available: " + 
                 std::to_string(entry.getAvailableCapacity()) + ")", "ResourceManager");
    return true;
}

size_t ResourceManager::releaseAllForOwner(int ownerId) {
    std::lock_guard<std::mutex> lock(resourceMutex_);

    auto ownerIt = ownerAllocations_.find(ownerId);
    if (ownerIt == ownerAllocations_.end()) {
        return 0;
    }

    size_t totalReleased = 0;
    for (const auto& [resId, count] : ownerIt->second) {
        auto resEntryIt = resourceTable_.find(resId);
        if (resEntryIt != resourceTable_.end()) {
            size_t actualRel = std::min(count, resEntryIt->second.allocatedCapacity);
            resEntryIt->second.allocatedCapacity -= actualRel;
            totalReleased += actualRel;
        }
    }

    ownerAllocations_.erase(ownerIt);
    Logger::info("Released all resources (" + std::to_string(totalReleased) + " units) for Owner #" + 
                 std::to_string(ownerId), "ResourceManager");
    return totalReleased;
}

size_t ResourceManager::getTotalCapacity(const std::string& resourceId) const {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    auto it = resourceTable_.find(resourceId);
    return (it != resourceTable_.end()) ? it->second.totalCapacity : 0;
}

size_t ResourceManager::getAllocatedCapacity(const std::string& resourceId) const {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    auto it = resourceTable_.find(resourceId);
    return (it != resourceTable_.end()) ? it->second.allocatedCapacity : 0;
}

size_t ResourceManager::getAvailableCapacity(const std::string& resourceId) const {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    auto it = resourceTable_.find(resourceId);
    return (it != resourceTable_.end()) ? it->second.getAvailableCapacity() : 0;
}

bool ResourceManager::hasResource(const std::string& resourceId) const {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    return resourceTable_.find(resourceId) != resourceTable_.end();
}

SystemResourceSnapshot ResourceManager::getSnapshot() const {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    SystemResourceSnapshot snapshot;
    snapshot.totalAllocated = 0;
    snapshot.totalAvailable = 0;

    for (const auto& [id, entry] : resourceTable_) {
        snapshot.resources.push_back(entry);
        snapshot.totalAllocated += entry.allocatedCapacity;
        snapshot.totalAvailable += entry.getAvailableCapacity();
    }
    return snapshot;
}

void ResourceManager::reset() {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    for (auto& [id, entry] : resourceTable_) {
        entry.allocatedCapacity = 0;
    }
    ownerAllocations_.clear();
    pageTable_.clear();
    Logger::info("All managed resources and owner allocations reset.", "ResourceManager");
}

// --- Backward Compatible Memory Page Implementations ---

bool ResourceManager::requestMemoryPage(int transactionId, int pageId) {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    if (pageTable_.find(pageId) != pageTable_.end()) {
        Logger::warn("Page conflict: Page #" + std::to_string(pageId) + 
                     " already allocated to Tx #" + std::to_string(pageTable_[pageId]), "ResourceManager");
        return false;
    }

    if (pageTable_.size() >= totalPages_) {
        Logger::warn("Memory exhausted: Page allocation failed for Tx #" + std::to_string(transactionId), "ResourceManager");
        return false;
    }

    pageTable_[pageId] = transactionId;
    Logger::info("Allocated Page #" + std::to_string(pageId) + " to Tx #" + std::to_string(transactionId), "ResourceManager");
    return true;
}

void ResourceManager::releaseMemoryPages(int transactionId) {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    for (auto it = pageTable_.begin(); it != pageTable_.end(); ) {
        if (it->second == transactionId) {
            it = pageTable_.erase(it);
        } else {
            ++it;
        }
    }
    Logger::info("Released all memory pages for Tx #" + std::to_string(transactionId), "ResourceManager");
}

size_t ResourceManager::getAvailablePages() const {
    std::lock_guard<std::mutex> lock(resourceMutex_);
    return (totalPages_ >= pageTable_.size()) ? (totalPages_ - pageTable_.size()) : 0;
}

size_t ResourceManager::getTotalPages() const {
    return totalPages_;
}

void ResourceManager::simulatePageAccess(int transactionId, int virtualAddress) {
    int pageNumber = virtualAddress / 4096; // 4KB page size
    int offset = virtualAddress % 4096;
    Logger::info("Tx #" + std::to_string(transactionId) + " translated Virtual Address 0x" + 
                 std::to_string(virtualAddress) + " -> Page " + std::to_string(pageNumber) + 
                 ", Offset " + std::to_string(offset), "ResourceManager");
}

void ResourceManager::demonstrateResourceManagement() {
    std::cout << "\n----------------------------------------\n";
    std::cout << "    PAYLOCK RESOURCE MANAGEMENT DEMO    \n";
    std::cout << "----------------------------------------\n\n";

    ResourceManager rm;
    rm.registerResource("BUFFER_SLOTS", ResourceType::ACCOUNT_RECORD, 5);

    std::cout << "1. Initial Resource State:\n";
    std::cout << rm.getSnapshot().toString() << "\n";

    std::cout << "2. Allocating 3 BUFFER_SLOTS for Transaction T1001:\n";
    rm.allocateResource("BUFFER_SLOTS", 3, 1001);

    std::cout << "3. Allocating 2 BUFFER_SLOTS for Transaction T1002:\n";
    rm.allocateResource("BUFFER_SLOTS", 2, 1002);

    std::cout << "\n4. Attempting to allocate 1 more BUFFER_SLOT for T1003 (Exhaustion Demo):\n";
    auto res = rm.allocateResource("BUFFER_SLOTS", 1, 1003);
    std::cout << "Result: " << (res == ResourceAllocationResult::EXHAUSTED ? "REJECTED (CAPACITY EXHAUSTED)" : "GRANTED") << "\n";

    std::cout << "\n5. Current State During Saturation:\n";
    std::cout << rm.getSnapshot().toString() << "\n";

    std::cout << "6. Releasing resources for T1001 (Commit / Unwind):\n";
    rm.releaseAllForOwner(1001);

    std::cout << "7. Re-attempting allocation for T1003:\n";
    res = rm.allocateResource("BUFFER_SLOTS", 1, 1003);
    std::cout << "Result: " << (res == ResourceAllocationResult::GRANTED ? "SUCCESSFULLY GRANTED" : "FAILED") << "\n";

    std::cout << "\n8. Final Resource State:\n";
    std::cout << rm.getSnapshot().toString() << "\n";
}

} // namespace paylock
