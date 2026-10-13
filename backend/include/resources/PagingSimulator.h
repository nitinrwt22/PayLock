#ifndef PAYLOCK_PAGING_SIMULATOR_H
#define PAYLOCK_PAGING_SIMULATOR_H

#include <vector>
#include <unordered_map>
#include <string>
#include <memory>
#include <mutex>

namespace paylock {

/**
 * @brief Record of a single page reference event.
 */
struct PagingEvent {
    size_t stepIndex{0};
    int pageRequested{-1};
    bool isHit{false};
    int frameAllocated{-1};       // Frame index (0 to frameCount - 1), or -1 if invalid
    int evictedPage{-1};          // Evicted page number, or -1 if none
    std::vector<int> frameOccupants; // Snapshot of pages in each frame (-1 for empty)
    std::string description;

    std::string toString() const;
};

/**
 * @brief Overall summary statistics for paging simulation.
 */
struct PagingStatistics {
    size_t totalReferences{0};
    size_t pageHits{0};
    size_t pageFaults{0};
    size_t pageReplacements{0};
    size_t occupiedFrames{0};
    size_t frameCount{0};
    double hitRatio{0.0};
    double faultRatio{0.0};

    std::string toString() const;
};

/**
 * @brief Extensible interface for page replacement algorithms.
 */
class PageReplacementPolicy {
public:
    virtual ~PageReplacementPolicy() = default;

    virtual std::string getName() const = 0;

    /**
     * @brief Called whenever a page is accessed (hit or newly loaded).
     */
    virtual void recordAccess(int pageNumber, size_t accessTime) = 0;

    /**
     * @brief Selects the victim frame index according to policy.
     * @param frames Current frame table mapping frameIndex -> pageNumber
     * @return Frame index to evict
     */
    virtual int selectVictim(const std::vector<int>& frames) = 0;

    /**
     * @brief Called when a page is evicted from memory.
     */
    virtual void onPageEvicted(int pageNumber) = 0;

    /**
     * @brief Resets policy internal state.
     */
    virtual void reset() = 0;
};

/**
 * @brief Least Recently Used (LRU) page replacement policy.
 * Evicts the page that has not been accessed for the longest period of time.
 */
class LRUReplacementPolicy : public PageReplacementPolicy {
public:
    LRUReplacementPolicy() = default;

    std::string getName() const override { return "LRU (Least Recently Used)"; }
    void recordAccess(int pageNumber, size_t accessTime) override;
    int selectVictim(const std::vector<int>& frames) override;
    void onPageEvicted(int pageNumber) override;
    void reset() override;

private:
    std::unordered_map<int, size_t> lastAccessTime_;
};

/**
 * @brief Deterministic educational Paging Simulator demonstrating OS memory management.
 *
 * NOTE: This is an educational abstraction demonstrating virtual pages, physical frames,
 * page tables, page faults, and LRU replacement. It does not inspect OS virtual memory
 * nor does it replace MySQL InnoDB's buffer pool.
 */
class PagingSimulator {
public:
    /**
     * @brief Constructs a paging simulator.
     * @param frameCount Number of physical frames (must be > 0 for valid execution)
     * @param policy Replacement policy (defaults to LRU if nullptr)
     */
    explicit PagingSimulator(size_t frameCount = 3, std::unique_ptr<PageReplacementPolicy> policy = nullptr);

    /**
     * @brief Accesses a virtual page and executes paging logic.
     * @param pageNumber Virtual page number requested
     * @return Event record detailing hit/fault, frame, and eviction
     */
    PagingEvent accessPage(int pageNumber);

    /**
     * @brief Processes a sequence of page references.
     * @param sequence Reference string (e.g. {7, 0, 1, 2, 0, 3, 0, 4, ...})
     * @return Aggregated statistics
     */
    PagingStatistics processSequence(const std::vector<int>& sequence);

    /**
     * @brief Resets frames, page table, history, and statistics to clean state.
     */
    void reset();

    // --- State Inquiries ---
    size_t getFrameCount() const;
    size_t getOccupiedFrameCount() const;
    bool isPageInMemory(int pageNumber) const;
    int getFrameForPage(int pageNumber) const;
    std::vector<int> getFrameTable() const;
    std::unordered_map<int, int> getPageTable() const;
    const std::vector<PagingEvent>& getHistory() const;
    PagingStatistics getStatistics() const;
    bool isValid() const;

    // --- Demonstration Fixture ---
    static PagingStatistics demonstrateLRUPaging();

private:
    mutable std::mutex simulatorMutex_;
    size_t frameCount_{0};
    bool isValid_{false};
    std::unique_ptr<PageReplacementPolicy> policy_;

    size_t currentTime_{0};
    size_t totalReferences_{0};
    size_t pageHits_{0};
    size_t pageFaults_{0};
    size_t pageReplacements_{0};

    // frameIndex -> pageNumber (-1 if empty)
    std::vector<int> frames_;

    // pageNumber -> frameIndex
    std::unordered_map<int, int> pageTable_;

    std::vector<PagingEvent> history_;
};

} // namespace paylock

#endif // PAYLOCK_PAGING_SIMULATOR_H
