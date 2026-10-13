#include "deadlock/DeadlockDetector.h"
#include "logging/Logger.h"
#include <algorithm>
#include <sstream>
#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <cassert>
#include <set>

namespace paylock {

// =========================================================================
// WaitForGraph Implementation
// =========================================================================

void WaitForGraph::addVertex(TransactionId txId) {
    vertices_.insert(txId);
    if (adjList_.find(txId) == adjList_.end()) {
        adjList_[txId] = {};
    }
}

bool WaitForGraph::hasVertex(TransactionId txId) const {
    return vertices_.find(txId) != vertices_.end();
}

const std::unordered_set<TransactionId>& WaitForGraph::getVertices() const {
    return vertices_;
}

bool WaitForGraph::addEdge(TransactionId waitingTxId, TransactionId holdingTxId) {
    addVertex(waitingTxId);
    addVertex(holdingTxId);

    auto& neighbors = adjList_[waitingTxId];
    if (std::find(neighbors.begin(), neighbors.end(), holdingTxId) == neighbors.end()) {
        neighbors.push_back(holdingTxId);
        return true;
    }
    return false; // Avoid duplicate edges
}

bool WaitForGraph::removeEdge(TransactionId waitingTxId, TransactionId holdingTxId) {
    auto it = adjList_.find(waitingTxId);
    if (it != adjList_.end()) {
        auto& neighbors = it->second;
        auto edgeIt = std::find(neighbors.begin(), neighbors.end(), holdingTxId);
        if (edgeIt != neighbors.end()) {
            neighbors.erase(edgeIt);
            return true;
        }
    }
    return false;
}

bool WaitForGraph::hasEdge(TransactionId waitingTxId, TransactionId holdingTxId) const {
    auto it = adjList_.find(waitingTxId);
    if (it != adjList_.end()) {
        return std::find(it->second.begin(), it->second.end(), holdingTxId) != it->second.end();
    }
    return false;
}

void WaitForGraph::removeVertex(TransactionId txId) {
    vertices_.erase(txId);
    adjList_.erase(txId);

    // Remove incoming edges from all other vertices
    for (auto& [u, neighbors] : adjList_) {
        neighbors.erase(std::remove(neighbors.begin(), neighbors.end(), txId), neighbors.end());
    }
}

void WaitForGraph::clear() {
    vertices_.clear();
    adjList_.clear();
}

const AdjacencyList& WaitForGraph::getAdjacencyList() const {
    return adjList_;
}

std::vector<TransactionId> WaitForGraph::getDependencies(TransactionId txId) const {
    auto it = adjList_.find(txId);
    if (it != adjList_.end()) {
        return it->second;
    }
    return {};
}

std::vector<std::pair<TransactionId, TransactionId>> WaitForGraph::getAllEdges() const {
    std::vector<std::pair<TransactionId, TransactionId>> edges;
    for (const auto& [u, neighbors] : adjList_) {
        for (TransactionId v : neighbors) {
            edges.emplace_back(u, v);
        }
    }
    return edges;
}

size_t WaitForGraph::vertexCount() const {
    return vertices_.size();
}

size_t WaitForGraph::edgeCount() const {
    size_t count = 0;
    for (const auto& [u, neighbors] : adjList_) {
        count += neighbors.size();
    }
    return count;
}

bool WaitForGraph::empty() const {
    return vertices_.empty();
}

std::string WaitForGraph::toString() const {
    std::ostringstream ss;
    ss << "Wait-For Graph (Vertices: " << vertices_.size() << ", Edges: " << edgeCount() << "):\n";
    if (vertices_.empty()) {
        ss << "  (Empty graph)\n";
        return ss.str();
    }

    std::vector<TransactionId> sortedVertices(vertices_.begin(), vertices_.end());
    std::sort(sortedVertices.begin(), sortedVertices.end());

    for (TransactionId u : sortedVertices) {
        auto it = adjList_.find(u);
        if (it != adjList_.end() && !it->second.empty()) {
            for (TransactionId v : it->second) {
                ss << "  " << DeadlockDetector::formatTx(u) << " -> " << DeadlockDetector::formatTx(v) << "\n";
            }
        } else {
            ss << "  " << DeadlockDetector::formatTx(u) << " (no dependencies)\n";
        }
    }
    return ss.str();
}

void WaitForGraph::display() const {
    std::cout << "\n=================================================================\n";
    std::cout << "                 PAYLOCK WAIT-FOR GRAPH (WFG)                    \n";
    std::cout << "=================================================================\n";
    std::cout << toString();
    std::cout << "=================================================================\n\n";
}

// =========================================================================
// DeadlockReport Implementation
// =========================================================================

std::string DeadlockReport::toString() const {
    std::ostringstream ss;
    ss << "=================================================================\n";
    ss << "                    DEADLOCK DETECTION REPORT                    \n";
    ss << "=================================================================\n";
    if (deadlockDetected) {
        ss << "Status: DEADLOCK DETECTED!\n";
        ss << "Cycles Detected: " << cycles.size() << "\n";
        for (size_t i = 0; i < cycles.size(); ++i) {
            ss << "  Cycle " << (i + 1) << ": " << DeadlockDetector::formatCycle(cycles[i]) << "\n";
        }
        ss << "Involved Transactions: ";
        std::vector<TransactionId> sortedTx(involvedTransactions.begin(), involvedTransactions.end());
        std::sort(sortedTx.begin(), sortedTx.end());
        for (size_t i = 0; i < sortedTx.size(); ++i) {
            if (i > 0) ss << ", ";
            ss << DeadlockDetector::formatTx(sortedTx[i]);
        }
        ss << "\n";
    } else {
        ss << "Status: NO DEADLOCK DETECTED\n";
        ss << "Cycles Detected: 0\n";
    }

    ss << "Graph Dependencies (" << dependencies.size() << " edge(s)):\n";
    if (dependencies.empty()) {
        ss << "  None\n";
    } else {
        for (const auto& [u, v] : dependencies) {
            ss << "  " << DeadlockDetector::formatTx(u) << " -> " << DeadlockDetector::formatTx(v) << "\n";
        }
    }
    ss << "=================================================================\n";
    return ss.str();
}

void DeadlockReport::logReport() const {
    if (deadlockDetected) {
        Logger::error("Deadlock detected", "DeadlockDetector");
        for (const auto& cycle : cycles) {
            Logger::error("Cycle: " + DeadlockDetector::formatCycle(cycle), "DeadlockDetector");
        }
    } else {
        Logger::info("Deadlock detection completed", "DeadlockDetector");
        Logger::info("No deadlock detected", "DeadlockDetector");
    }
}

// =========================================================================
// DeadlockDetector Implementation
// =========================================================================

DeadlockDetector::DeadlockDetector(LockManager* lockManager)
    : lockManager_(lockManager) {}

std::string DeadlockDetector::formatTx(TransactionId txId) {
    return "T" + std::to_string(txId);
}

std::string DeadlockDetector::formatCycle(const std::vector<TransactionId>& cycle) {
    std::ostringstream ss;
    for (size_t i = 0; i < cycle.size(); ++i) {
        if (i > 0) ss << " -> ";
        ss << formatTx(cycle[i]);
    }
    return ss.str();
}

std::vector<TransactionId> DeadlockDetector::canonicalizeCycle(const std::vector<TransactionId>& rawCycle) {
    if (rawCycle.size() <= 2) {
        return rawCycle;
    }

    // rawCycle is [v0, v1, ..., vk-1, v0]
    std::vector<TransactionId> nodes(rawCycle.begin(), rawCycle.end() - 1);
    auto minIt = std::min_element(nodes.begin(), nodes.end());
    std::rotate(nodes.begin(), minIt, nodes.end());
    nodes.push_back(nodes.front()); // Close cycle
    return nodes;
}

void DeadlockDetector::dfsCycleDetect(TransactionId current,
                                     const WaitForGraph& graph,
                                     std::unordered_map<TransactionId, int>& colors,
                                     std::vector<TransactionId>& path,
                                     std::vector<std::vector<TransactionId>>& detectedCycles) {
    colors[current] = 1; // GRAY: in recursion stack
    path.push_back(current);

    auto neighbors = graph.getDependencies(current);
    std::sort(neighbors.begin(), neighbors.end());

    for (TransactionId neighbor : neighbors) {
        int neighborColor = colors[neighbor]; // default 0: WHITE
        if (neighborColor == 1) { // GRAY: Back edge discovered! Directed cycle found
            auto it = std::find(path.begin(), path.end(), neighbor);
            if (it != path.end()) {
                std::vector<TransactionId> rawCycle(it, path.end());
                rawCycle.push_back(neighbor); // closing edge
                detectedCycles.push_back(canonicalizeCycle(rawCycle));
            }
        } else if (neighborColor == 0) { // WHITE: unvisited neighbor
            dfsCycleDetect(neighbor, graph, colors, path, detectedCycles);
        }
    }

    path.pop_back();
    colors[current] = 2; // BLACK: completely processed
}

std::vector<std::vector<TransactionId>> DeadlockDetector::detectCycles(const WaitForGraph& graph) const {
    std::unordered_map<TransactionId, int> colors;
    std::vector<TransactionId> path;
    std::vector<std::vector<TransactionId>> detectedCycles;

    std::vector<TransactionId> sortedVertices(graph.getVertices().begin(), graph.getVertices().end());
    std::sort(sortedVertices.begin(), sortedVertices.end());

    for (TransactionId v : sortedVertices) {
        if (colors[v] == 0) {
            dfsCycleDetect(v, graph, colors, path, detectedCycles);
        }
    }

    // Deduplicate canonical cycles
    std::set<std::vector<TransactionId>> uniqueCycles(detectedCycles.begin(), detectedCycles.end());
    return std::vector<std::vector<TransactionId>>(uniqueCycles.begin(), uniqueCycles.end());
}

WaitForGraph DeadlockDetector::buildWaitForGraph(const LockTableSnapshot& snapshot) const {
    Logger::info("Building Wait-For Graph", "DeadlockDetector");
    WaitForGraph graph;

    // Add all active transactions as vertices
    for (const auto& [txId, holdings] : snapshot.transactionHoldings) {
        graph.addVertex(txId);
    }

    for (const auto& [accId, res] : snapshot.resources) {
        for (int holder : res.holders) {
            graph.addVertex(holder);
        }
        for (const auto& req : res.waitQueue) {
            graph.addVertex(req.transactionId);
        }
    }

    // Add directed wait-for edges: waitingTx -> holdingTx
    for (const auto& [accId, res] : snapshot.resources) {
        if (res.holders.empty() || res.waitQueue.empty()) {
            continue;
        }

        for (const auto& req : res.waitQueue) {
            TransactionId waitingTx = req.transactionId;
            LockMode requestedMode = req.mode;

            for (TransactionId holdingTx : res.holders) {
                if (holdingTx == waitingTx) {
                    continue; // Skip self dependencies
                }

                bool incompatible = false;
                if (requestedMode == LockMode::EXCLUSIVE) {
                    // EXCLUSIVE conflicts with any granted lock
                    incompatible = true;
                } else if (requestedMode == LockMode::SHARED) {
                    // SHARED conflicts only if resource is currently held in EXCLUSIVE mode
                    if (res.currentMode.has_value() && *res.currentMode == LockMode::EXCLUSIVE) {
                        incompatible = true;
                    }
                }

                if (incompatible) {
                    if (graph.addEdge(waitingTx, holdingTx)) {
                        Logger::info("Dependency: " + formatTx(waitingTx) + " waits for " + formatTx(holdingTx), "DeadlockDetector");
                    }
                }
            }
        }
    }

    return graph;
}

WaitForGraph DeadlockDetector::buildWaitForGraph(const LockManager& lockManager) const {
    return buildWaitForGraph(lockManager.getSnapshot());
}

WaitForGraph DeadlockDetector::buildWaitForGraph() const {
    if (lockManager_) {
        return buildWaitForGraph(lockManager_->getSnapshot());
    }
    std::lock_guard<std::mutex> lock(manualGraphMutex_);
    return manualGraph_;
}

DeadlockReport DeadlockDetector::detectDeadlocks(const WaitForGraph& graph) const {
    DeadlockReport report;
    report.timestamp = std::chrono::system_clock::now();
    report.dependencies = graph.getAllEdges();
    report.cycles = detectCycles(graph);
    report.deadlockDetected = !report.cycles.empty();

    for (const auto& cycle : report.cycles) {
        // Exclude the duplicate closing vertex from involved set
        for (size_t i = 0; i + 1 < cycle.size(); ++i) {
            report.involvedTransactions.insert(cycle[i]);
        }
    }

    report.summary = report.toString();
    report.logReport();
    return report;
}

DeadlockReport DeadlockDetector::detectDeadlocks(const LockTableSnapshot& snapshot) const {
    WaitForGraph graph = buildWaitForGraph(snapshot);
    return detectDeadlocks(graph);
}

DeadlockReport DeadlockDetector::detectDeadlocks(const LockManager& lockManager) const {
    return detectDeadlocks(lockManager.getSnapshot());
}

DeadlockReport DeadlockDetector::detectDeadlocks() const {
    if (lockManager_) {
        return detectDeadlocks(*lockManager_);
    }
    std::lock_guard<std::mutex> lock(manualGraphMutex_);
    return detectDeadlocks(manualGraph_);
}

bool DeadlockDetector::hasDeadlock(std::vector<TransactionId>& cycleOut) const {
    std::lock_guard<std::mutex> lock(manualGraphMutex_);
    auto cycles = detectCycles(manualGraph_);
    if (!cycles.empty()) {
        cycleOut = cycles.front();
        return true;
    }
    cycleOut.clear();
    return false;
}

void DeadlockDetector::addWaitForEdge(int waitingTxId, int holdingTxId) {
    std::lock_guard<std::mutex> lock(manualGraphMutex_);
    Logger::info("Adding WFG Edge: " + formatTx(waitingTxId) + " -> " + formatTx(holdingTxId), "DeadlockDetector");
    manualGraph_.addEdge(waitingTxId, holdingTxId);
}

void DeadlockDetector::removeWaitForEdge(int waitingTxId, int holdingTxId) {
    std::lock_guard<std::mutex> lock(manualGraphMutex_);
    manualGraph_.removeEdge(waitingTxId, holdingTxId);
}

void DeadlockDetector::removeTransaction(int txId) {
    std::lock_guard<std::mutex> lock(manualGraphMutex_);
    manualGraph_.removeVertex(txId);
}

void DeadlockDetector::clearGraph() {
    std::lock_guard<std::mutex> lock(manualGraphMutex_);
    manualGraph_.clear();
}

std::unordered_map<int, std::vector<int>> DeadlockDetector::getGraphSnapshot() const {
    std::lock_guard<std::mutex> lock(manualGraphMutex_);
    return manualGraph_.getAdjacencyList();
}

DeadlockReport DeadlockDetector::demonstrateControlledDeadlock(LockManager& lm) {
    Logger::info("Initializing Safe Controlled Deadlock Demonstration...", "DeadlockDetector");

    const int acc1 = 101;
    const int acc2 = 102;
    const int tx1 = 1;
    const int tx2 = 2;

    // Ensure clean initial state
    lm.releaseAllLocks(tx1);
    lm.releaseAllLocks(tx2);

    // Step 1: Establish baseline locks
    // T1 holds X(A101)
    // T2 holds X(A102)
    bool g1 = lm.acquireLock(tx1, acc1, LockMode::EXCLUSIVE);
    bool g2 = lm.acquireLock(tx2, acc2, LockMode::EXCLUSIVE);
    assert(g1 && g2);

    std::atomic<bool> t1AcquiredAcc2{false};
    std::atomic<bool> t2AcquiredAcc1{false};

    // Step 2: Thread 1 requests conflicting lock X(A102)
    std::thread th1([&]() {
        if (lm.acquireLock(tx1, acc2, LockMode::EXCLUSIVE)) {
            t1AcquiredAcc2.store(true);
            lm.releaseLock(tx1, acc2);
        }
    });

    // Deterministically synchronize: wait until T1 is queued for acc2
    while (lm.getWaitingRequests(acc2).empty()) {
        std::this_thread::yield();
    }

    // Step 3: Thread 2 requests conflicting lock X(A101)
    std::thread th2([&]() {
        if (lm.acquireLock(tx2, acc1, LockMode::EXCLUSIVE)) {
            t2AcquiredAcc1.store(true);
            lm.releaseLock(tx2, acc1);
        }
    });

    // Deterministically synchronize: wait until T2 is queued for acc1
    while (lm.getWaitingRequests(acc1).empty()) {
        std::this_thread::yield();
    }

    // Both threads are now waiting in LockManager wait queues.
    // Build WFG from LockManager snapshot and run cycle detection:
    DeadlockDetector detector(&lm);
    DeadlockReport report = detector.detectDeadlocks();

    // Step 4: Safe, controlled cleanup without performing recovery.
    // Release locks so blocked threads can cleanly acquire, release, and join.
    lm.releaseLock(tx1, acc1);
    th2.join();

    lm.releaseLock(tx2, acc2);
    th1.join();

    lm.releaseAllLocks(tx1);
    lm.releaseAllLocks(tx2);

    Logger::info("Controlled Deadlock Demonstration Terminated Safely", "DeadlockDetector");
    return report;
}

} // namespace paylock
