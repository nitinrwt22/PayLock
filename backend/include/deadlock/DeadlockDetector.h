#ifndef PAYLOCK_DEADLOCK_DETECTOR_H
#define PAYLOCK_DEADLOCK_DETECTOR_H

#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <string>

namespace paylock {

/**
 * @brief Wait-For Graph (WFG) and Cycle Detection subsystem for Deadlock Handling.
 * 
 * OS & DBMS Concept:
 * - Directed graph where nodes = Transactions, directed edge (Ti -> Tj) = Ti is waiting for a lock held by Tj.
 * - Deadlock condition: Existence of a directed cycle in the WFG.
 */
class DeadlockDetector {
public:
    DeadlockDetector() = default;

    // Wait-For Graph Management
    void addWaitForEdge(int waitingTxId, int holdingTxId);
    void removeWaitForEdge(int waitingTxId, int holdingTxId);
    void removeTransaction(int txId);
    void clearGraph();

    // Cycle Detection (Tarjan's / DFS algorithm interface)
    bool hasDeadlock(std::vector<int>& cycleOut);
    std::unordered_map<int, std::vector<int>> getGraphSnapshot() const;

private:
    bool dfsCheck(int current,
                  std::unordered_map<int, int>& visited,
                  std::vector<int>& path,
                  std::vector<int>& cycleOut);

    mutable std::mutex graphMutex_;
    std::unordered_map<int, std::vector<int>> adjacencyList_;
};

} // namespace paylock

#endif // PAYLOCK_DEADLOCK_DETECTOR_H
