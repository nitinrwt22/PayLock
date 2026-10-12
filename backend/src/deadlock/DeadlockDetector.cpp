#include "deadlock/DeadlockDetector.h"
#include "logging/Logger.h"
#include <algorithm>

namespace paylock {

void DeadlockDetector::addWaitForEdge(int waitingTxId, int holdingTxId) {
    std::lock_guard<std::mutex> lock(graphMutex_);
    Logger::info("Adding WFG Edge: Tx #" + std::to_string(waitingTxId) + 
                 " -> Tx #" + std::to_string(holdingTxId), "DeadlockDetector");

    adjacencyList_[waitingTxId].push_back(holdingTxId);
}

void DeadlockDetector::removeWaitForEdge(int waitingTxId, int holdingTxId) {
    std::lock_guard<std::mutex> lock(graphMutex_);
    auto it = adjacencyList_.find(waitingTxId);
    if (it != adjacencyList_.end()) {
        auto& neighbors = it->second;
        neighbors.erase(std::remove(neighbors.begin(), neighbors.end(), holdingTxId), neighbors.end());
        if (neighbors.empty()) {
            adjacencyList_.erase(it);
        }
    }
}

void DeadlockDetector::removeTransaction(int txId) {
    std::lock_guard<std::mutex> lock(graphMutex_);
    adjacencyList_.erase(txId);

    // Remove incoming edges
    for (auto& pair : adjacencyList_) {
        auto& neighbors = pair.second;
        neighbors.erase(std::remove(neighbors.begin(), neighbors.end(), txId), neighbors.end());
    }
}

void DeadlockDetector::clearGraph() {
    std::lock_guard<std::mutex> lock(graphMutex_);
    adjacencyList_.clear();
}

bool DeadlockDetector::hasDeadlock(std::vector<int>& cycleOut) {
    std::lock_guard<std::mutex> lock(graphMutex_);
    cycleOut.clear();

    // 0 = unvisited, 1 = visiting (in recursion stack), 2 = visited
    std::unordered_map<int, int> visited;
    std::vector<int> currentPath;

    for (const auto& pair : adjacencyList_) {
        int node = pair.first;
        if (visited[node] == 0) {
            if (dfsCheck(node, visited, currentPath, cycleOut)) {
                Logger::warn("Deadlock Cycle Detected in WFG!", "DeadlockDetector");
                return true;
            }
        }
    }
    return false;
}

bool DeadlockDetector::dfsCheck(int current,
                                std::unordered_map<int, int>& visited,
                                std::vector<int>& path,
                                std::vector<int>& cycleOut) {
    visited[current] = 1;
    path.push_back(current);

    auto it = adjacencyList_.find(current);
    if (it != adjacencyList_.end()) {
        for (int neighbor : it->second) {
            if (visited[neighbor] == 1) {
                // Cycle found
                auto cycleStart = std::find(path.begin(), path.end(), neighbor);
                cycleOut.assign(cycleStart, path.end());
                cycleOut.push_back(neighbor);
                return true;
            }
            if (visited[neighbor] == 0) {
                if (dfsCheck(neighbor, visited, path, cycleOut)) {
                    return true;
                }
            }
        }
    }

    path.pop_back();
    visited[current] = 2;
    return false;
}

std::unordered_map<int, std::vector<int>> DeadlockDetector::getGraphSnapshot() const {
    std::lock_guard<std::mutex> lock(graphMutex_);
    return adjacencyList_;
}

} // namespace paylock
