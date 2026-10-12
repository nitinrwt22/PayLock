#include "resources/ResourceManager.h"
#include "logging/Logger.h"

namespace paylock {

ResourceManager::ResourceManager(size_t totalMemoryPages)
    : totalPages_(totalMemoryPages) {}

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
    return totalPages_ - pageTable_.size();
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

} // namespace paylock
