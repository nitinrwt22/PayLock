# PayLock Screen Inventory & Design Mapping
**Stitch Project ID:** `14592152822615229882`  
**Reference Assets Path:** `frontend/design/reference/`  

This document catalogs every screen generated and verified from the Google Stitch project, defining its functional scope, layout hierarchy, and backend API bindings.

---

## Screen Catalog

| Screen Key | Screen Title | Stitch Screen ID | Reference HTML | Reference Screenshot | API Mappings |
| :--- | :--- | :--- | :--- | :--- | :--- |
| `overview` | System Overview & Command Center | `c2997babdf274df8a69e895bcc479986` | [overview.html](file:///Users/nitinrawat/PayLock/frontend/design/reference/overview.html) | [overview.png](file:///Users/nitinrawat/PayLock/frontend/design/reference/overview.png) | `/api/v1/health`<br>`/api/v1/system/stats`<br>`/api/v1/transactions` |
| `transactions` | Transactions Monitor & Audit Log | `1f71e971fafa4b428d5ac37825eb7d46` | [transactions.html](file:///Users/nitinrawat/PayLock/frontend/design/reference/transactions.html) | [transactions.png](file:///Users/nitinrawat/PayLock/frontend/design/reference/transactions.png) | `/api/v1/transactions`<br>`/api/v1/accounts`<br>`/api/v1/transactions/transfer` |
| `concurrency` | Concurrency Monitor & Thread Pool | `003ff27588984bac99d2abb724064910` | [concurrency.html](file:///Users/nitinrawat/PayLock/frontend/design/reference/concurrency.html) | [concurrency.png](file:///Users/nitinrawat/PayLock/frontend/design/reference/concurrency.png) | `/api/v1/concurrency/status`<br>`/api/v1/locks`<br>`/api/v1/locks/wait-for-graph`<br>`/api/v1/deadlocks` |
| `transaction-detail` | Txn Inspector & Execution Lifecycle | `1868e2eadaaa4fb4a01e13eeb70e5c1c` | [transaction_detail.html](file:///Users/nitinrawat/PayLock/frontend/design/reference/transaction_detail.html) | [transaction_detail.png](file:///Users/nitinrawat/PayLock/frontend/design/reference/transaction_detail.png) | `/api/v1/transactions/:id` |

---

## Screen Breakdowns

### 1. Screen: Overview (`overview`)
- **Stitch ID:** `c2997babdf274df8a69e895bcc479986`
- **Purpose:** Central operational dashboard displaying cluster state, POSIX runtime telemetry, throughput, 8-KPI metric strip, real-time transaction ingestion feed, and quick system actions.
- **Key Components:**
  - **Telemetry Stream Header:** Operational status pulse, Engine Clock uptime timer, Sync State trigger.
  - **8-KPI Bento Metric Strip:** Total Txn, Committed, Rolled Back, Pthreads, 2PL Locks, Deadlocks Detected, Frame Cache, Avg Latency.
  - **Live Concurrency Chart / Lock Heatmap:** 2PL lock contention distribution and worker queue depth.
  - **Recent Transactions Ledger:** 6 most recent transactions with status indicators, amounts, and execution times.
  - **System Resources Summary:** CPU worker load, Buffer Pool paging hit ratio, MySQL connection health.

### 2. Screen: Transactions Monitor (`transactions`)
- **Stitch ID:** `1f71e971fafa4b428d5ac37825eb7d46`
- **Purpose:** Full historical and real-time audit log of all financial transfers and database transactions.
- **Key Components:**
  - **Search & Filter Bar:** Full-text filter by TXN hash, source/dest account, date range, and status pills (`All`, `Committed`, `Processing`, `Waiting`, `Rolled Back`, `Deadlock`).
  - **Dense Audit Table:** Columns for TXN ID, Timestamp, From Account, To Account, Amount (USD), Lock Mode (`EXCLUSIVE` / `SHARED`), Thread Worker ID, Latency (ms), Status Badge, and Action inspect button.
  - **Pagination Controls:** Per-page selector, record counts, next/prev navigation.
  - **Transfer Modal Trigger:** Invokes the ACID transfer modal to initiate live backend payments.

### 3. Screen: Concurrency Monitor & Thread Pool (`concurrency`)
- **Stitch ID:** `003ff27588984bac99d2abb724064910`
- **Purpose:** Deep diagnostic view of OS thread pool execution, 2-Phase Locking state, Wait-For Graph (WFG) cycle detection, and deadlock victim resolution.
- **Key Components:**
  - **Thread Pool Pipeline Visualization:** Visual flow board displaying workers `T-01` to `T-08`, active states (Running, Mutex Wait, Committed), and execution stages.
  - **Active Locks Table:** Resource ID, Lock Type (Shared/Exclusive), Holder Txn, Waiters Queue, Granted Timestamp.
  - **Wait-For Graph (WFG) & Cycle Monitor:** Graph depiction of transaction dependency edges $T_i \to T_j$. Highlights detected cycle in amber/red with victim selection details.
  - **Deadlock Recovery History:** Aborted victim transactions, rollback timestamps, and lock release audit.

### 4. Screen: Transaction Inspector & Detail (`transaction-detail`)
- **Stitch ID:** `1868e2eadaaa4fb4a01e13eeb70e5c1c`
- **Purpose:** Microsecond-level autopsy of an individual transaction's ACID lifecycle.
- **Key Components:**
  - **Transaction Header:** UUID hash, execution status badge, total latency, block height/sequence number.
  - **ACID Verification Badge Bar:** Atomicity (WAL committed), Consistency (Account invariants verified), Isolation (`SERIALIZABLE 2PL`), Durability (fsync complete).
  - **Latency Microsecond Breakdown:** Segmented bar detailing Queue time, Mutex lock wait, SQL Execution, and WAL log flush.
  - **Lock Acquisition Timeline:** Step-by-step lock requests, grants, and release events.
  - **SQL Execution Log & State Diffs:** Before/after account balance diffs with currency formatting.

---

## Shared Layout Shell Elements
Every screen shares the exact same base frame:
1. **Left Navigation Sidebar (`w-64` / 256px fixed):**
   - Logo + `PAYLOCK` label + `C++ v2.4` live pulse.
   - `New Transaction` primary button (`N` shortcut).
   - Core Pipeline, Concurrency & Locks, Diagnostics navigation groups with active highlight indicators.
   - Live worker/mutex telemetry mini-widget + user identity profile card (`Nitin Rawat`).
2. **Top Header Bar (`h-14` / 56px fixed, offset `pl-64`):**
   - Dynamic path breadcrumbs (`PAYLOCK / ...`).
   - Cluster health badge (`CLUSTER: STABLE • 4.2k TPS`).
   - Global search input (`⌘K`).
   - `Simulate Traffic` button, notifications with unread badge, avatar.
3. **Acid Transfer Modal:**
   - Central interactive dialog to execute real transfers via `POST /api/v1/transactions/transfer`.
