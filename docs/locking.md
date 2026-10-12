# PayLock Lock Manager Specification (Step 4)

## 1. What the Lock Manager Does
The **Lock Manager** (`paylock::LockManager`) provides fine-grained, application-level concurrency control over shared payment resources (accounts).
While operating-system synchronization primitives (`std::mutex`) protect internal C++ memory data structures from race conditions, the **Lock Manager** controls logical, transactional access to business entities (`ACCOUNT:A101`, `ACCOUNT:A102`) across competing transactions.

### Key Capabilities:
- Application-level 2-Phase Locking (2PL) semantics.
- Granular account-row locking, allowing transactions touching disjoint accounts to proceed concurrently.
- Strict distinction between Read (Shared) and Write (Exclusive) operations.
- Non-busy waiting queue management backed by `std::condition_variable`.
- Transaction lock ownership tracking (`transactionId` $\rightarrow$ set of held locks).
- Deterministic multi-resource acquisition to avoid circular AB-BA waits during transfers.
- Real-time ASCII Lock Table state monitoring for CLI diagnostics and dashboards.

---

## 2. Shared Locks (S)
A **Shared Lock** (Read Lock) allows concurrent reading of a payment resource.
- Multiple transactions may hold a Shared lock on the same resource simultaneously.
- Typical use case: `checkBalance(accountId)` acquires a Shared lock on `ACCOUNT:A<id>` so that concurrent audit readers do not block one another, but balance writers are prevented from causing dirty reads.
- Example:
  - $T_1 \rightarrow S(A101)$ (GRANTED)
  - $T_2 \rightarrow S(A101)$ (GRANTED simultaneously)

---

## 3. Exclusive Locks (X)
An **Exclusive Lock** (Write Lock) guarantees single-transaction access to a payment resource.
- Only one transaction may hold an Exclusive lock on a resource at any given moment.
- No other transaction may hold either a Shared or an Exclusive lock concurrently.
- Typical use cases: `transfer(A, B)`, `deposit(A)`, `withdraw(A)` require Exclusive locks on involved accounts.
- Example:
  - $T_1 \rightarrow X(A101)$ (GRANTED)
  - $T_2 \rightarrow X(A101)$ (WAITING in queue until $T_1$ releases)

---

## 4. Compatibility Matrix
Lock compatibility is evaluated through a centralized, uniform compatibility check:

| Requested Mode | Existing Mode: None | Existing Mode: Shared (S) | Existing Mode: Exclusive (X) |
|---|---|---|---|
| **Shared (S)** | **Compatible (YES)** | **Compatible (YES)** | **Incompatible (NO)** |
| **Exclusive (X)** | **Compatible (YES)** | **Incompatible (NO)** | **Incompatible (NO)** |

### Invariant Rules:
1. $S + S \implies$ Compatible (Multiple readers allowed).
2. $S + X \implies$ Incompatible (Reader blocks if writer holds).
3. $X + S \implies$ Incompatible (Writer blocks if reader holds).
4. $X + X \implies$ Incompatible (Writer blocks if writer holds).

Implemented centrally in `LockManager::isCompatible(requestedMode, currentMode, numHolders)` without redundant duplications across the codebase.

---

## 5. Lock Ownership Tracking
The Lock Manager tracks ownership in two reciprocal dimensions:
1. **Resource Perspective (`lockTable_`)**:
   - Maps each `accountId` to a `ResourceLockEntry` containing:
     - Current granted mode (`std::optional<LockMode>`)
     - Set of active holder transaction IDs (`std::unordered_set<int> holders`)
     - FIFO wait queue (`std::vector<LockRequest> waitQueue`)
2. **Transaction Perspective (`transactionHoldings_`)**:
   - Maps each `transactionId` to its held accounts and modes (`accountId` $\rightarrow$ `LockMode`).
   - Enables fast querying:
     - `getLocksForTransaction(txId)`
     - `getDetailedLocksForTransaction(txId)`
     - Atomic `releaseAllLocks(txId)` during transaction commit or rollback.

---

## 6. Waiting Queues & Condition Variables
When an incoming lock request is incompatible with the existing granted locks on a resource, the request is not rejected; instead, it enters an orderly **Waiting Queue**:
- **Non-Busy Waiting**: No `sleep()` loops or tight polling. Threads suspend execution using `std::condition_variable lockCv_`.
- **FIFO Fairness Policy**: Incoming requests cannot bypass earlier waiting requests in `waitQueue`. This prevents starvation of write transactions by continuous incoming read locks.
- **Wakeup Notification**: When any transaction releases a lock via `releaseLock` or `releaseAllLocks`, `lockCv_.notify_all()` is signaled. Woken threads re-evaluate their eligibility predicate against current holders and queue position.

---

## 7. Lock Acquisition Protocol (`acquireLock` / `tryAcquireLock`)
When a transaction requests a lock:
1. Acquire internal `tableMutex_`.
2. Check reentrancy: If the transaction already holds the requested lock, return `true` immediately.
3. Check compatibility: If the resource is unheld or compatible (and queue head is free), grant immediately, record ownership in `holders` and `transactionHoldings_`, and return `true`.
4. If incompatible: Enqueue `LockRequest` with state `WAITING`.
5. Suspend thread on `lockCv_` using a predicate ensuring it only wakes when it is at the front of the queue and compatible with current holders.
6. Upon waking and satisfying the predicate, remove request from `waitQueue`, grant ownership, and return `true`.

---

## 8. Lock Release Protocol (`releaseLock` / `releaseAllLocks`)
When a transaction completes:
1. Remove `transactionId` from the resource's `holders` set.
2. If `holders` becomes empty, reset `currentMode` to `std::nullopt`.
3. Erase from `transactionHoldings_[txId]`.
4. Signal `lockCv_.notify_all()`.
5. `releaseAllLocks(txId)` iterates all resources held by `txId`, releases them under a single lock acquisition of `tableMutex_`, and notifies waiting threads.

---

## 9. Lock Upgrade Protocol (`upgradeLock`: S $\rightarrow$ X)
When a transaction holding a Shared lock requests an Exclusive lock on the same resource:
- **Solo Holder Case**: If the requesting transaction is the **sole** holder of the Shared lock, the upgrade is granted immediately ($S \rightarrow X$) without waiting.
- **Multi-Holder Case**: If other transactions also hold Shared locks, the requesting transaction waits on `lockCv_` until all other holders release their Shared locks.
- Once only the requesting transaction remains, it transitions the resource to `LockMode::EXCLUSIVE`.

---

## 10. Resource Identifiers
All resource identifiers follow a strict, deterministic naming convention:
- Formatted as `ACCOUNT:A<id>` (e.g., `ACCOUNT:A101`, `ACCOUNT:A102`).
- Account ID `0` is reserved for external sinks/sources (cash ATM or external system).
- Every account consistently maps to its exact integer ID across all modules, ensuring clean correlation in logs, the lock table, and future wait-for graphs.

---

## 11. Deterministic Multi-Resource Lock Ordering
To prevent circular wait deadlocks (AB-BA deadlocks) during peer-to-peer transfers, `LockManager::acquireTransferLocks` enforces a **deterministic ascending lock order**:
```text
Transfer from Account 105 to Account 101:
  first  = min(101, 105) -> 101
  second = max(101, 105) -> 105

  1. acquireLock(txId, 101, EXCLUSIVE)
  2. acquireLock(txId, 105, EXCLUSIVE)
```
Even if another concurrent transfer moves money in reverse ($101 \rightarrow 105$), both transactions lock $101$ first, then $105$, completely eliminating circular wait conditions during normal transfers.

---

## 12. Thread Safety Architecture
The Lock Manager itself is concurrently invoked by multiple worker threads:
- **Separation of Concerns**:
  - `tableMutex_` protects only the Lock Manager's in-memory data structures (`lockTable_`, `transactionHoldings_`).
  - `tableMutex_` is **NEVER** held across database queries, network calls, or disk I/O.
- **RAII Scoping**: Every method uses `std::lock_guard` or `std::unique_lock` to guarantee exception-safe mutex acquisition and release.
- **Safe Iterator Handling**: All map lookups are refreshed upon waking from condition variable waits to avoid iterator invalidation caused by table rehashing.

---

## 13. Current Limitations & Why Deadlock Detection is Excluded
In accordance with Step 4 specifications:
- **No Wait-For Graph (WFG)**: Dynamic cycle detection is intentionally not implemented in this step.
- **No Victim Selection / Rollback**: If an arbitrary circular wait is constructed outside of deterministic ordering, threads block on `lockCv_` until released or timed out.
- **Scope Boundary**: Deadlock Detection via Wait-For Graph (WFG) and DFS cycle search is the objective of **Step 5**. Deadlock Recovery and automated victim abort are the objective of **Step 6**.

---

## 14. Lock Table CLI Monitoring Output
Below is an example of the real-time Lock Table output produced by `LockManager::displayLockTable()`:

```text
=================================================================
                      PAYLOCK LOCK TABLE                         
=================================================================
RESOURCE        HOLDERS     TYPE        STATE       WAITING QUEUE
-----------------------------------------------------------------
ACCOUNT:A102    T1003, T1002SHARED      GRANTED     None
ACCOUNT:A101    T1001       EXCLUSIVE   GRANTED     None
=================================================================
```
