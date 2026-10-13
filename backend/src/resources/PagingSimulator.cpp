#include "resources/PagingSimulator.h"
#include "logging/Logger.h"
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cassert>
#include <limits>

namespace paylock {

std::string PagingEvent::toString() const {
    std::ostringstream oss;
    oss << "Step " << std::setw(2) << stepIndex << " | "
        << "Page " << std::setw(2) << pageRequested << " -> "
        << (isHit ? "[HIT ]" : "[FAULT]")
        << " | Frame: " << std::setw(2) << frameAllocated << " | ";

    if (evictedPage != -1) {
        oss << "Evicted Page: " << std::setw(2) << evictedPage;
    } else {
        oss << "Evicted: None   ";
    }

    oss << " | Frames: [";
    for (size_t i = 0; i < frameOccupants.size(); ++i) {
        if (frameOccupants[i] == -1) {
            oss << " - ";
        } else {
            oss << " " << frameOccupants[i] << " ";
        }
        if (i + 1 < frameOccupants.size()) oss << "|";
    }
    oss << "]";
    return oss.str();
}

std::string PagingStatistics::toString() const {
    std::ostringstream oss;
    oss << "=================================================================\n";
    oss << "                 PAYLOCK PAGING SIMULATOR REPORT                 \n";
    oss << "=================================================================\n";
    oss << "Total Page References : " << totalReferences << "\n";
    oss << "Page Hits             : " << pageHits << " ("
        << std::fixed << std::setprecision(2) << (hitRatio * 100.0) << "%)\n";
    oss << "Page Faults           : " << pageFaults << " ("
        << std::fixed << std::setprecision(2) << (faultRatio * 100.0) << "%)\n";
    oss << "Page Replacements     : " << pageReplacements << "\n";
    oss << "Frames Configured     : " << frameCount << "\n";
    oss << "Frames Occupied       : " << occupiedFrames << " / " << frameCount << "\n";
    oss << "=================================================================\n";
    return oss.str();
}

// --- LRUReplacementPolicy Implementation ---

void LRUReplacementPolicy::recordAccess(int pageNumber, size_t accessTime) {
    lastAccessTime_[pageNumber] = accessTime;
}

int LRUReplacementPolicy::selectVictim(const std::vector<int>& frames) {
    int victimFrame = -1;
    size_t oldestTime = std::numeric_limits<size_t>::max();

    for (size_t i = 0; i < frames.size(); ++i) {
        int page = frames[i];
        if (page != -1) {
            auto it = lastAccessTime_.find(page);
            size_t accessTime = (it != lastAccessTime_.end()) ? it->second : 0;
            if (accessTime < oldestTime) {
                oldestTime = accessTime;
                victimFrame = static_cast<int>(i);
            }
        }
    }
    return victimFrame;
}

void LRUReplacementPolicy::onPageEvicted(int pageNumber) {
    lastAccessTime_.erase(pageNumber);
}

void LRUReplacementPolicy::reset() {
    lastAccessTime_.clear();
}

// --- PagingSimulator Implementation ---

PagingSimulator::PagingSimulator(size_t frameCount, std::unique_ptr<PageReplacementPolicy> policy)
    : frameCount_(frameCount),
      isValid_(frameCount > 0),
      policy_(policy ? std::move(policy) : std::make_unique<LRUReplacementPolicy>()) {
    if (isValid_) {
        frames_.assign(frameCount_, -1);
    }
}

PagingEvent PagingSimulator::accessPage(int pageNumber) {
    std::lock_guard<std::mutex> lock(simulatorMutex_);

    PagingEvent event;
    if (!isValid_) {
        event.pageRequested = pageNumber;
        event.isHit = false;
        event.frameAllocated = -1;
        event.evictedPage = -1;
        event.description = "Simulation invalid: 0 frames configured";
        return event;
    }

    currentTime_++;
    totalReferences_++;

    event.stepIndex = totalReferences_;
    event.pageRequested = pageNumber;

    auto it = pageTable_.find(pageNumber);
    if (it != pageTable_.end()) {
        // --- PAGE HIT ---
        pageHits_++;
        int frameIdx = it->second;
        policy_->recordAccess(pageNumber, currentTime_);

        event.isHit = true;
        event.frameAllocated = frameIdx;
        event.evictedPage = -1;
        event.frameOccupants = frames_;
        event.description = "Page hit in Frame #" + std::to_string(frameIdx);

        Logger::info("Paging HIT: Page #" + std::to_string(pageNumber) + 
                     " found in Frame #" + std::to_string(frameIdx), "PagingSimulator");
    } else {
        // --- PAGE FAULT ---
        pageFaults_++;
        event.isHit = false;

        // Check for free frame
        int targetFrame = -1;
        for (size_t i = 0; i < frames_.size(); ++i) {
            if (frames_[i] == -1) {
                targetFrame = static_cast<int>(i);
                break;
            }
        }

        if (targetFrame != -1) {
            // Load into empty frame
            frames_[targetFrame] = pageNumber;
            pageTable_[pageNumber] = targetFrame;
            policy_->recordAccess(pageNumber, currentTime_);

            event.frameAllocated = targetFrame;
            event.evictedPage = -1;
            event.frameOccupants = frames_;
            event.description = "Page fault loaded into empty Frame #" + std::to_string(targetFrame);

            Logger::info("Paging FAULT: Page #" + std::to_string(pageNumber) + 
                         " loaded into free Frame #" + std::to_string(targetFrame), "PagingSimulator");
        } else {
            // All frames occupied -> Replacement required
            pageReplacements_++;
            int victimFrame = policy_->selectVictim(frames_);
            assert(victimFrame >= 0 && victimFrame < static_cast<int>(frames_.size()));

            int evictedPage = frames_[victimFrame];
            pageTable_.erase(evictedPage);
            policy_->onPageEvicted(evictedPage);

            frames_[victimFrame] = pageNumber;
            pageTable_[pageNumber] = victimFrame;
            policy_->recordAccess(pageNumber, currentTime_);

            event.frameAllocated = victimFrame;
            event.evictedPage = evictedPage;
            event.frameOccupants = frames_;
            event.description = "Page fault: Evicted Page #" + std::to_string(evictedPage) + 
                                " from Frame #" + std::to_string(victimFrame);

            Logger::info("Paging REPLACEMENT: Evicted Page #" + std::to_string(evictedPage) + 
                         " from Frame #" + std::to_string(victimFrame) + 
                         " for Page #" + std::to_string(pageNumber), "PagingSimulator");
        }
    }

    // Invariant verifications
    for (size_t i = 0; i < frames_.size(); ++i) {
        if (frames_[i] != -1) {
            assert(pageTable_.find(frames_[i]) != pageTable_.end());
            assert(pageTable_[frames_[i]] == static_cast<int>(i));
        }
    }

    history_.push_back(event);
    return event;
}

PagingStatistics PagingSimulator::processSequence(const std::vector<int>& sequence) {
    for (int page : sequence) {
        accessPage(page);
    }
    return getStatistics();
}

void PagingSimulator::reset() {
    std::lock_guard<std::mutex> lock(simulatorMutex_);
    if (isValid_) {
        frames_.assign(frameCount_, -1);
    }
    pageTable_.clear();
    history_.clear();
    if (policy_) {
        policy_->reset();
    }
    currentTime_ = 0;
    totalReferences_ = 0;
    pageHits_ = 0;
    pageFaults_ = 0;
    pageReplacements_ = 0;
    Logger::info("Paging simulator state reset.", "PagingSimulator");
}

size_t PagingSimulator::getFrameCount() const {
    return frameCount_;
}

size_t PagingSimulator::getOccupiedFrameCount() const {
    std::lock_guard<std::mutex> lock(simulatorMutex_);
    size_t count = 0;
    for (int page : frames_) {
        if (page != -1) count++;
    }
    return count;
}

bool PagingSimulator::isPageInMemory(int pageNumber) const {
    std::lock_guard<std::mutex> lock(simulatorMutex_);
    return pageTable_.find(pageNumber) != pageTable_.end();
}

int PagingSimulator::getFrameForPage(int pageNumber) const {
    std::lock_guard<std::mutex> lock(simulatorMutex_);
    auto it = pageTable_.find(pageNumber);
    return (it != pageTable_.end()) ? it->second : -1;
}

std::vector<int> PagingSimulator::getFrameTable() const {
    std::lock_guard<std::mutex> lock(simulatorMutex_);
    return frames_;
}

std::unordered_map<int, int> PagingSimulator::getPageTable() const {
    std::lock_guard<std::mutex> lock(simulatorMutex_);
    return pageTable_;
}

const std::vector<PagingEvent>& PagingSimulator::getHistory() const {
    std::lock_guard<std::mutex> lock(simulatorMutex_);
    return history_;
}

PagingStatistics PagingSimulator::getStatistics() const {
    std::lock_guard<std::mutex> lock(simulatorMutex_);
    PagingStatistics stats;
    stats.totalReferences = totalReferences_;
    stats.pageHits = pageHits_;
    stats.pageFaults = pageFaults_;
    stats.pageReplacements = pageReplacements_;
    stats.frameCount = frameCount_;

    size_t occupied = 0;
    for (int p : frames_) {
        if (p != -1) occupied++;
    }
    stats.occupiedFrames = occupied;

    stats.hitRatio = (totalReferences_ > 0)
        ? (static_cast<double>(pageHits_) / totalReferences_)
        : 0.0;
    stats.faultRatio = (totalReferences_ > 0)
        ? (static_cast<double>(pageFaults_) / totalReferences_)
        : 0.0;

    return stats;
}

bool PagingSimulator::isValid() const {
    return isValid_;
}

PagingStatistics PagingSimulator::demonstrateLRUPaging() {
    std::cout << "\n----------------------------------------\n";
    std::cout << "      PAYLOCK LRU PAGING SIMULATION     \n";
    std::cout << "----------------------------------------\n\n";

    const size_t frames = 3;
    const std::vector<int> referenceString = {
        7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1
    };

    std::cout << "Configuration:\n";
    std::cout << "  Physical Frames : " << frames << "\n";
    std::cout << "  Reference String: { ";
    for (size_t i = 0; i < referenceString.size(); ++i) {
        std::cout << referenceString[i] << (i + 1 < referenceString.size() ? ", " : " }\n\n");
    }

    std::cout << "Execution Trace:\n";
    std::cout << "-----------------------------------------------------------------\n";

    PagingSimulator simulator(frames);
    for (int page : referenceString) {
        auto ev = simulator.accessPage(page);
        std::cout << ev.toString() << "\n";
    }
    std::cout << "-----------------------------------------------------------------\n\n";

    auto stats = simulator.getStatistics();
    std::cout << stats.toString() << "\n";
    return stats;
}

} // namespace paylock
