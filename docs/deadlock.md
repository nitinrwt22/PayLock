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

# Part II: Deadlock Recovery Specification (Step 6)

## 11. Objective and Recovery Architecture

Deadlock Recovery in PayLock provides a controlled, coordinated mechanism to resolve circular wait dependencies detected by `DeadlockDetector`. Rather than abruptly killing worker threads or corrupting state, PayLock employs a **cooperative abort and rollback architecture**.

### Recovery Flow
```text
DeadlockDetector
       │
       ▼ (Deadlock Report)
RecoveryManager
       │
       ▼
Select Victim Transaction (Deterministic Policy)
       │
       ▼
Request Safe Abort (Cooperative cancellation flag & LockManager abort registration)
       │
       ▼
TransactionManager
       ├──► MySQL ROLLBACK on owning connection
       └──► Update Transaction Status to 'ROLLED_BACK'
       │
       ▼
Release Victim's Locks (LockManager::releaseAllLocks)
       │
       ▼
Notify Waiting Transactions (std::condition_variable broadcast)
       │
       ▼
Surviving Transactions Acquire Granted Locks & Continue to Commit
```

---

## 12. Victim Selection Policies

The victim must strictly be an active member of the detected circular wait cycle. PayLock implements four deterministic victim selection policies:

1. **`YOUNGEST_TRANSACTION` (Default & Recommended)**:
   - Selects the transaction with the highest `TransactionId` within the cycle.
   - Rationale: In PayLock, transaction IDs are monotonically increasing. The highest transaction ID corresponds to the newest transaction, which has invested the least processing time and acquired fewer system resources, minimizing rollback cost.

2. **`FEWEST_LOCKS_HELD`**:
   - Queries `LockManager::getHeldLocksCount()` for each transaction in the cycle.
   - Selects the transaction holding the smallest number of locks.
   - Breaks ties deterministically by selecting the highest transaction ID.

3. **`LOWEST_RECOVERY_PRIORITY` / `LOWEST_COST`**:
   - Evaluates real system metrics tracked by PayLock (locks held and transaction ID).
   - In accordance with project requirements, no artificial or ungrounded priority metrics are invented.

4. **`HIGHEST_TRANSACTION_ID`**:
   - Direct numerical selection of $\max(T_i \in \text{Cycle})$.
   - 100% deterministic and repeatable for unit tests.

### Deterministic Cycle Membership Guarantee
`RecoveryManager::selectVictim()` parses the detected cycle $[v_0, v_1, \dots, v_k, v_0]$ and deduplicates its vertices into a set of unique cycle members. Under no circumstance is a transaction outside the detected cycle considered or selected as a victim.

---

## 13. Safe Transaction Abort & Cooperative Unwinding

A primary architectural rule in PayLock is:
> **Never terminate a worker thread forcefully (`std::terminate()`, `pthread_cancel()`, or OS signals).**

### Cooperative Abort Mechanism
1. **Cancellation Flag in TransactionManager**:
   - `TransactionManager::requestAbort(transactionId, reason)` sets an abort request flag for the target transaction.
   - Safe checkpoint checks: `TransactionManager::transfer()`, `deposit()`, and `withdraw()` evaluate `isAbortRequested(transactionId)` before executing critical database updates and before issuing `COMMIT`.
   - If an abort is flagged, the transaction immediately halts operations, executes `ROLLBACK`, marks status `ROLLED_BACK`, and returns `false`.

2. **Interrupting Blocked Threads in LockManager**:
   - When a transaction is blocked waiting inside `LockManager::acquireLock()` on a `std::condition_variable`, it cannot check status flags until awakened.
   - `LockManager::abortWaitingTransaction(transactionId)` registers the transaction in `abortedTransactions_` and notifies all waiting condition variables (`lockCv_.notify_all()`).
   - The waiting predicate evaluates `abortedTransactions_.count(transactionId)`.
   - The blocked victim unblocks immediately, removes its pending request from the resource's `waitQueue`, and returns `false`.
   - The worker cleanly unwinds its call stack, avoiding deadlocks and thread leaks.

---

## 14. MySQL Rollback Integration & Balance Conservation

1. **Owning Connection Integrity**:
   - Database operations in MySQL require transactions to be rolled back on their own active connection or through the transaction manager controlling that connection.
   - When `RecoveryManager` initiates rollback via `TransactionManager::rollbackTransaction()`, the active transaction issues:
     ```sql
     ROLLBACK;
     UPDATE transactions SET status = 'ROLLED_BACK' WHERE transaction_id = ?;
     INSERT INTO transaction_logs (transaction_id, action, details) VALUES (?, 'DEADLOCK_ABORTED', ...);
     ```
2. **Conservation of Balances**:
   - In uncommitted database transactions, pending balance deductions or additions are discarded by MySQL InnoDB engine.
   - Balances revert to their exact pre-transaction state.
   - Sum of all balances before deadlock equals sum of all balances after recovery ($\sum B_{\text{initial}} = \sum B_{\text{final}}$).

3. **Already-Committed Race Condition Handling**:
   - If a transaction committed before the recovery manager's abort signal was processed, `RecoveryManager` detects this via `TransactionManager::isCommitted()`.
   - The system refuses to issue a meaningless or dangerous rollback, logs an explanatory notice, and records `RollbackOutcome::ALREADY_COMMITTED`.

---

## 15. Lock Release Ordering & Waiter Resumption

1. **Safe Release Order**:
   - First, the victim transaction stops modifying resources and cancels its wait requests.
   - Second, database changes are rolled back.
   - Third, `LockManager::releaseAllLocks(victimTxId)` removes all lock holdings owned by the victim.
   - Locks belonging to surviving transactions remain completely intact.
2. **Waking Surviving Transactions**:
   - Upon lock release, `LockManager` broadcasts on `lockCv_`.
   - Surviving waiting threads wake up and re-evaluate queue head compatibility.
   - The survivor at the front of the queue is granted the lock and proceeds normally to execute and commit.

---

## 16. Failure Handling & Idempotency

- **Empty / Incomplete Cycle**: If a report contains fewer than 2 distinct transactions, recovery returns `RecoveryResult` with `attempted = false` and `RollbackOutcome::SKIPPED`.
- **Duplicate Recovery Requests**: `RecoveryManager` caches processed cycles in `processedCycles_`. If the exact same deadlock report is submitted twice, the duplicate is ignored and reported as skipped, preventing double-rollbacks or spurious lock releases.
- **Rollback Failures**: If database connectivity fails during rollback, `RecoveryResult` captures `RollbackOutcome::DATABASE_ERROR` and logs a detailed error with the failure reason.

---

## 17. Comprehensive Step 6 Test Results

All 67 tests across PayLock pass deterministically without timing assumptions or test hangs:

| Test Case | Description | Result |
|---|---|---|
| **Test 52** | Victim selection from detected cycle | **PASSED** |
| **Test 53** | Victim selection does not select unrelated transactions | **PASSED** |
| **Test 54** | Fewest locks held victim selection policy | **PASSED** |
| **Test 55** | Safe cooperative abort request | **PASSED** |
| **Test 56** | Actual MySQL database rollback on victim abort | **PASSED** |
| **Test 57** | Transaction state after recovery is ROLLED_BACK | **PASSED** |
| **Test 58** | Release of victim-owned locks from lock table | **PASSED** |
| **Test 59** | Preservation of surviving transactions' locks | **PASSED** |
| **Test 60** | Waiting transaction notification and progress | **PASSED** |
| **Test 61** | Victim cannot commit after an accepted abort | **PASSED** |
| **Test 62** | Already-committed transaction handled safely without rollback | **PASSED** |
| **Test 63** | Structured failure reporting on invalid recovery report | **PASSED** |
| **Test 64** | Duplicate recovery request idempotency | **PASSED** |
| **Test 65** | Multi-threaded recovery terminates bounded in <50ms | **PASSED** |
| **Test 66** | Full controlled deadlock recovery end-to-end demonstration | **PASSED** |
| **Test 67** | Database consistency and total balance conservation | **PASSED** |

---

## 18. Step 6 Limitations & Next Steps

1. **Automated Background Thread**: Currently, recovery is invoked deterministically via `RecoveryManager::recover()`. In Step 7 (Resource Management & Prevention), background monitoring threads can periodically poll for deadlocks and invoke recovery autonomously.
2. **Transaction Retry**: Aborted victims currently remain in `ROLLED_BACK` status. Automatic transaction replay with randomized exponential backoff can be added in subsequent layers.

