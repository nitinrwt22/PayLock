# PayLock Deadlock Detection Specification (Step 5)

## 1. What Deadlock Means in PayLock

In **PayLock**, transactions execute concurrently across distributed banking and payment resources (`ACCOUNT:A101`, `ACCOUNT:A102`, etc.).
A **deadlock** occurs when two or more transactions are permanently blocked in a state of mutual waiting because each transaction holds a lock that another transaction in the set requires to proceed.

Without intervention, deadlocked transactions remain blocked indefinitely, exhausting system thread pools and holding precious database resources hostage.
Step 5 implements **Deadlock Detection**—the systematic discovery and reporting of circular waiting dependencies across active transactions—laying the foundation for subsequent recovery (Step 6).

---

## 2. Four Necessary Conditions for Deadlock

According to Coffman et al. (1971), a deadlock can occur if and only if all four of the following conditions hold simultaneously:

1. **Mutual Exclusion**:
   - Resources involved cannot be shared simultaneously in conflicting modes.
   - In PayLock, payment accounts require **Exclusive Locks (X)** for writing (`transfer`, `deposit`, `withdraw`), granting sole access to a single transaction at any given moment.

2. **Hold and Wait**:
   - A transaction currently holding at least one resource lock is permitted to request additional resource locks without first relinquishing the locks it already holds.
   - In PayLock, a multi-account transfer (`T1: A101 -> A102`) acquires an exclusive lock on `A101` and holds it while attempting to acquire an exclusive lock on `A102`.

3. **No Preemption**:
   - Locks cannot be forcibly confiscated or preempted from a transaction while it is running.
   - Locks are released only voluntarily by the holding transaction upon `COMMIT`, `ROLLBACK`, or explicit release.

4. **Circular Wait**:
   - A closed chain of transactions $\{T_1, T_2, \dots, T_k\}$ exists such that $T_1$ is waiting for a lock held by $T_2$, $T_2$ is waiting for a lock held by $T_3$, ..., and $T_k$ is waiting for a lock held by $T_1$.

---

## 3. Wait-For Graph (WFG) Architecture

The **Wait-For Graph (WFG)** is a directed graph $G = (V, E)$ specifically designed to track resource dependency relationships among transactions:

- **Vertices ($V$)**: Every active transaction in the system (whether holding a lock or waiting in a queue).
- **Directed Edges ($E$)**: A directed edge $T_i \rightarrow T_j$ indicates that transaction $T_i$ is actively blocked waiting for a resource currently held by transaction $T_j$.

### Graph Invariants & Representation
- **Vertices**: Represented as `TransactionId` (`int`, e.g. `1`, `2`, `1001`).
- **Adjacency List**: `std::unordered_map<TransactionId, std::vector<TransactionId>>`.
- **No Duplicate Edges**: If transaction $T_1$ waits on multiple locks held by $T_2$, only a single directed edge $T_1 \rightarrow T_2$ exists.
- **Isolated Vertices Supported**: Transactions holding locks with no incoming or outgoing waits are explicitly represented as degree-0 vertices.

```text
T1 requests X(A101)  ──────►  T2 holds S(A101)
         │
         └─────────────────►  T3 holds S(A101)

Wait-For Graph:
T1 ──────► T2
 │
 └───────► T3
```

---

## 4. How Graph Edges are Constructed

The graph is constructed dynamically from an in-memory snapshot of the `LockManager`:

1. **Snapshot Isolation**:
   - Under `LockManager::tableMutex_`, an atomic point-in-time `LockTableSnapshot` is extracted.
   - The mutex is released immediately, ensuring graph construction and cycle detection never block normal transaction lock acquisitions.

2. **Vertex Population**:
   - All transactions with entries in `transactionHoldings` or `lockTable_` are added as vertices.

3. **Edge Evaluation**:
   For each resource (`accountId`):
   - For each waiting request $R_{\text{wait}} = (T_{\text{wait}}, \text{mode})$ in `waitQueue`:
     - For each granted lock holder $T_{\text{holder}}$ in `holders`:
       - If $T_{\text{wait}} == T_{\text{holder}}$, skip (no self-loops).
       - **Exclusive Request ($X$)**: Conflicts with all current holders (Shared or Exclusive). An edge $T_{\text{wait}} \rightarrow T_{\text{holder}}$ is added for every holder.
       - **Shared Request ($S$)**: Conflicts only if the resource is currently held in Exclusive ($X$) mode. If currently held in $X$ mode, an edge $T_{\text{wait}} \rightarrow T_{\text{holder}}$ is added.
       - Edges are never inferred from transaction timestamps or order; they reflect genuine lock contention.

---

## 5. Depth-First Search (DFS) Cycle Detection Algorithm

Cycle detection is implemented using Depth-First Search with 3-color vertex marking (recursion-stack tracking):

### Conceptual States
- **WHITE (0)**: Vertex has not yet been visited.
- **GRAY (1)**: Vertex is currently in the recursion stack (active path).
- **BLACK (2)**: Vertex and all its descendants have been completely processed.

### Algorithm Flow
1. Sort vertices deterministically by transaction ID.
2. For each vertex $u \in V$:
   - If `colors[u] == WHITE`, invoke `dfsCycleDetect(u)`.
3. In `dfsCycleDetect(current)`:
   - Mark `colors[current] = GRAY`; push `current` onto `path`.
   - For each neighbor $v$ of `current`:
     - If `colors[v] == GRAY`:
       - **Back edge detected!** A directed cycle exists.
       - Extract the cycle: slice `path` from $v$ to `current`, and append $v$ as the closing edge:
         $$\text{Cycle} = [v, \dots, \text{current}, v]$$
       - Canonicalize the cycle and store it.
     - Else if `colors[v] == WHITE`:
       - Recursively invoke `dfsCycleDetect(v)`.
   - Pop `current` from `path`; mark `colors[current] = BLACK`.
4. Return all canonicalized, deduplicated cycles.

### Cycle Canonicalization & Duplicate Avoidance
To prevent reporting the exact same directed cycle multiple times from different starting vertices:
- Given raw cycle $[v_0, v_1, \dots, v_{k-1}, v_0]$, take the base nodes $[v_0, \dots, v_{k-1}]$.
- Find the minimum transaction ID $v_{\min} = \min(v_0, \dots, v_{k-1})$.
- Rotate the sequence so $v_{\min}$ is the first element, preserving directed edge order.
- Append $v_{\min}$ to close the cycle.
- Insert into `std::set<std::vector<TransactionId>>` for complete uniqueness.

---

## 6. Time and Space Complexity

- **Time Complexity**:
  - Graph Construction: $O(R \cdot (H + W))$ where $R$ is the number of resources, $H$ is the average holders per resource, and $W$ is the wait queue length.
  - Cycle Detection: Standard 3-color DFS visits each vertex once ($V$) and traverses each directed edge once ($E$).
  - For a graph with $V$ active transactions and $E$ wait dependencies, cycle detection runs in **$O(V + E)$** time.
  - Cycle extraction and canonicalization run in $O(L)$ where $L \le V$ is cycle length.
- **Space Complexity**:
  - Adjacency list: $O(V + E)$.
  - Visited color map: $O(V)$.
  - Recursion stack: $O(V)$.
  - Total auxiliary space is strictly **$O(V + E)$**.

---

## 7. Controlled Deadlock Demonstration

To demonstrate detection safely without indefinite hanging or non-deterministic thread sleeps, PayLock provides `DeadlockDetector::demonstrateControlledDeadlock(LockManager& lm)`:

### State Setup
- Two logical resources: `ACCOUNT:A101` and `ACCOUNT:A102`.
- Two transactions: $T_1$ and $T_2$.
- Step 1: $T_1$ acquires $X(\text{A101})$; $T_2$ acquires $X(\text{A102})$.
- Step 2: Thread 1 requests $X(\text{A102})$ and blocks in `waitQueue(A102)`. Coordinator verifies presence via `lm.getWaitingRequests(102)`.
- Step 3: Thread 2 requests $X(\text{A101})$ and blocks in `waitQueue(A101)`. Coordinator verifies presence via `lm.getWaitingRequests(101)`.

### Resulting Dependencies & Report
```text
DEADLOCK DETECTED!
Wait-For Graph:
  T1 -> T2 (waiting for A102)
  T2 -> T1 (waiting for A101)

Cycle:
  T1 -> T2 -> T1
```

### Safe Termination Without Recovery
- Once verified, the demonstration fixture releases $X(\text{A101})$ held by $T_1$, unblocking Thread 2.
- Thread 2 acquires, finishes, and releases $X(\text{A102})$, unblocking Thread 1.
- Both threads join cleanly without leaked threads, unhandled exceptions, or deadlocks.

---

## 8. Why Detection and Recovery are Separate

In accordance with strict operating systems and database design principles:
- **Detection (Step 5)** is purely observational:
  - Discovers and reports circular wait dependencies.
  - Analyzes graph topology and records participating transactions.
  - Leaves lock tables, waiting queues, and account balances 100% unaltered.
- **Recovery (Step 6)** is active and policy-driven:
  - Chooses an appropriate victim transaction based on policies (`YOUNGEST_TRANSACTION`, `FEWEST_LOCKS`, `LOWEST_COST`).
  - Aborts the victim, rolls back its database state, and releases its held locks to unblock surviving transactions.

Decoupling detection from recovery ensures detection algorithms can be tested, benchmarked, and monitored independently without risking unintended transaction rollbacks.

---

## 9. Comprehensive Test Suite Results

All 51 test cases across the system pass with 100% success:

| Test Case | Description | Result |
|---|---|---|
| **Test 36** | Empty Wait-For Graph produces 0 cycles and clean report | **PASSED** |
| **Test 37** | Graph without edges (Isolated vertices $T_1, T_2, T_3$) | **PASSED** |
| **Test 38** | Acyclic dependency chain ($T_1 \rightarrow T_2 \rightarrow T_3$) | **PASSED** |
| **Test 39** | Two-transaction cycle ($T_1 \rightarrow T_2 \rightarrow T_1$) | **PASSED** |
| **Test 40** | Three-transaction cycle ($T_1 \rightarrow T_2 \rightarrow T_3 \rightarrow T_1$) | **PASSED** |
| **Test 41** | Disconnected graph with cycle in one component | **PASSED** |
| **Test 42** | Cycle with outgoing non-cycle edge ($T_3 \rightarrow T_4$) | **PASSED** |
| **Test 43** | Multiple distinct cycles ($T_1 \leftrightarrow T_2$ and $T_3 \leftrightarrow T_4$) | **PASSED** |
| **Test 44** | Duplicate edge insertion prevention | **PASSED** |
| **Test 45** | Precise cycle membership with incoming and outgoing edges | **PASSED** |
| **Test 46** | Graph built directly from LockManager snapshot | **PASSED** |
| **Test 47** | Multi-holder shared lock dependency branching | **PASSED** |
| **Test 48** | Exclusive lock conflicting reader dependency | **PASSED** |
| **Test 49** | Live concurrent detection while threads actively wait | **PASSED** |
| **Test 50** | Verification that detection performs ZERO recovery | **PASSED** |
| **Test 51** | Controlled deadlock demonstration fixture safe termination | **PASSED** |

---

## 10. Current Limitations & Next Steps

1. **Step 6 Prerequisite**: When a deadlock is detected, the current implementation records the cycle and logs the warning, but does not abort transactions. Step 6 will integrate `RecoveryManager` to select victims and execute rollbacks.
2. **Detection Frequency**: Detection is currently event-triggered and on-demand. Step 6 will evaluate continuous periodic background detection versus on-wait invocation.
