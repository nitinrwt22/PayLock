# PayLock Deadlock Handling & Recovery Specification

## Overview
Deadlocks occur when two or more transactions are permanently blocked because each holds a lock that the other needs. PayLock establishes the foundation for Deadlock Detection, Wait-For Graph (WFG) analysis, and Recovery via Victim Selection.

---

## The Four Coffman Conditions
A deadlock can only arise if all four conditions hold simultaneously:
1. **Mutual Exclusion**: Resources (accounts) cannot be shared simultaneously in conflicting modes (Exclusive Write locks).
2. **Hold and Wait**: A transaction holds at least one lock while requesting additional locks.
3. **No Preemption**: Locks cannot be forcibly confiscated from a transaction before it completes.
4. **Circular Wait**: A closed chain of transactions exists where $T_1$ waits for $T_2$, and $T_2$ waits for $T_1$.

---

## Wait-For Graph (WFG) Architecture
- **Nodes**: Active transactions ($T_1, T_2, \dots, T_n$).
- **Directed Edges**: An edge $T_i \rightarrow T_j$ indicates transaction $T_i$ is blocked waiting for a lock held by transaction $T_j$.
- **Detection Algorithm**: Depth-First Search (DFS) / Tarjan's cycle detection algorithm detects directed cycles in $O(V + E)$ time complexity.

```text
       Tx #1001 (Holds Account 101, Waits for Account 102)
             │                                   ▲
             ▼                                   │
       Tx #1002 (Holds Account 102, Waits for Account 101)
                    [CYCLE DETECTED: DEADLOCK]
```

---

## Recovery Strategies
1. **Victim Selection**:
   - `YOUNGEST_TRANSACTION`: Abort the most recently initiated transaction to minimize wasted work.
   - `FEWEST_LOCKS_HELD`: Abort the transaction holding the fewest resources.
   - `LOWEST_COST_TO_ROLLBACK`: Abort the transaction with the smallest undo footprint.
2. **Rollback & Restart**:
   - Abort victim transaction via `ROLLBACK`.
   - Release all held locks via `LockManager::releaseAllLocks(victimId)`.
   - Unblock surviving transactions to proceed.
