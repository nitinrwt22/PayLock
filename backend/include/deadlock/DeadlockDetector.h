#ifndef PAYLOCK_DEADLOCK_DETECTOR_H
#define PAYLOCK_DEADLOCK_DETECTOR_H

#include "locking/LockManager.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <string>
#include <chrono>

namespace paylock {

using TransactionId = int;
using AdjacencyList = std::unordered_map<TransactionId, std::vector<TransactionId>>;

/**
 * @brief Represents a Wait-For Graph (WFG) where vertices are active transactions
 * and directed edges represent wait-for dependencies (Ti -> Tj means Ti is waiting
 * for a resource held by Tj).
 */
class WaitForGraph {
public:
    WaitForGraph() = default;

    // Vertex operations
    void addVertex(TransactionId txId);
    bool hasVertex(TransactionId txId) const;
    const std::unordered_set<TransactionId>& getVertices() const;

    // Edge operations (waitingTxId -> holdingTxId)
    bool addEdge(TransactionId waitingTxId, TransactionId holdingTxId);
    bool removeEdge(TransactionId waitingTxId, TransactionId holdingTxId);
    bool hasEdge(TransactionId waitingTxId, TransactionId holdingTxId) const;
    void removeVertex(TransactionId txId);
    void clear();

    // Query operations
    const AdjacencyList& getAdjacencyList() const;
    std::vector<TransactionId> getDependencies(TransactionId txId) const;
    std::vector<std::pair<TransactionId, TransactionId>> getAllEdges() const;
    size_t vertexCount() const;
    size_t edgeCount() const;
    bool empty() const;

    // Formatting & debug output
    std::string toString() const;
    void display() const;

private:
    std::unordered_set<TransactionId> vertices_;
    AdjacencyList adjList_;
};

/**
 * @brief Structured report describing the results of deadlock detection.
 */
struct DeadlockReport {
    bool deadlockDetected{false};
    std::vector<std::vector<TransactionId>> cycles; // Each cycle ends with the start node: e.g. T1 -> T2 -> T1
    std::unordered_set<TransactionId> involvedTransactions;
    std::vector<std::pair<TransactionId, TransactionId>> dependencies; // All directed edges in WFG
    std::chrono::system_clock::time_point timestamp;
    std::string summary;

    std::string toString() const;
    void logReport() const;
};

/**
 * @brief DeadlockDetector constructs Wait-For Graphs and detects circular dependencies.
 * 
 * OS & DBMS Principles:
 * - Four Coffman conditions for deadlock: Mutual Exclusion, Hold & Wait, No Preemption, Circular Wait.
 * - This module identifies the Circular Wait condition via directed cycles in the Wait-For Graph.
 * - Detection is strictly decoupled from Recovery (victim selection / rollback belong to Step 6).
 */
class DeadlockDetector {
public:
    explicit DeadlockDetector(LockManager* lockManager = nullptr);

    void setLockManager(LockManager* lockManager) noexcept { lockManager_ = lockManager; }
    LockManager* getLockManager() const noexcept { return lockManager_; }

    // WFG Construction
    WaitForGraph buildWaitForGraph() const;
    WaitForGraph buildWaitForGraph(const LockTableSnapshot& snapshot) const;
    WaitForGraph buildWaitForGraph(const LockManager& lockManager) const;

    // Cycle Detection (DFS with 3 colors: WHITE, GRAY, BLACK)
    std::vector<std::vector<TransactionId>> detectCycles(const WaitForGraph& graph) const;
    bool hasDeadlock(std::vector<TransactionId>& cycleOut) const; // Backwards compatible

    // High-level Detection & Reporting
    DeadlockReport detectDeadlocks() const;
    DeadlockReport detectDeadlocks(const LockManager& lockManager) const;
    DeadlockReport detectDeadlocks(const LockTableSnapshot& snapshot) const;
    DeadlockReport detectDeadlocks(const WaitForGraph& graph) const;

    // Manual WFG Management (Backwards Compatible)
    void addWaitForEdge(int waitingTxId, int holdingTxId);
    void removeWaitForEdge(int waitingTxId, int holdingTxId);
    void removeTransaction(int txId);
    void clearGraph();
    std::unordered_map<int, std::vector<int>> getGraphSnapshot() const;

    // Controlled Deadlock Demonstration Fixture
    static DeadlockReport demonstrateControlledDeadlock(LockManager& lm);

    // Helpers
    static std::string formatCycle(const std::vector<TransactionId>& cycle);
    static std::string formatTx(TransactionId txId);

private:
    // Helper to canonicalize a cycle so identical cycles have the same representation
    static std::vector<TransactionId> canonicalizeCycle(const std::vector<TransactionId>& rawCycle);

    // DFS implementation using WHITE (0), GRAY (1), BLACK (2) states
    static void dfsCycleDetect(TransactionId current,
                               const WaitForGraph& graph,
                               std::unordered_map<TransactionId, int>& colors,
                               std::vector<TransactionId>& path,
                               std::vector<std::vector<TransactionId>>& detectedCycles);

    LockManager* lockManager_{nullptr};

    // Internal manual graph for backward compatibility with manual addWaitForEdge tests
    mutable std::mutex manualGraphMutex_;
    WaitForGraph manualGraph_;
};

} // namespace paylock

#endif // PAYLOCK_DEADLOCK_DETECTOR_H
