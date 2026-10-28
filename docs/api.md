# PayLock REST API Specification (v1.0)

## 1. Overview and Architecture

The PayLock API Layer provides a high-performance, embedded C++ HTTP REST interface exposing the backend's core transaction processing, concurrency, locking, deadlock detection/recovery, and simulated resource management capabilities.

The API layer is built directly into the C++ application using **`cpp-httplib`** and **`nlohmann/json`**, maintaining zero dependency on external runtime environments (such as Node.js, Python, or Java).

```
                      +-----------------------------+
                      |   Frontend Web Dashboard    |
                      |   (Google Stitch Prototype) |
                      +--------------+--------------+
                                     |  HTTP REST (JSON)
                                     v
                      +-----------------------------+
                      |   PayLock HttpServer &     |
                      |      ApiController          |
                      +--------------+--------------+
                                     |
         +---------------------------+---------------------------+
         |                           |                           |
         v                           v                           v
+------------------+        +------------------+        +------------------+
| TransactionMgr   |        |   LockManager    |        | DeadlockDetector |
| (ACID Transfers) |        | (Rigorous 2PL)   |        |  & RecoveryMgr   |
+--------+---------+        +--------+---------+        +--------+---------+
         |                           |                           |
         +---------------------------+---------------------------+
                                     |
                                     v
                       +---------------------------+
                       |    MySQL 9.x Database     |
                       |    (InnoDB WAL Engine)    |
                       +---------------------------+
```

### Architecture Highlights:
- **Embedded C++ Native**: Direct linking with C++ backend services for minimal latency and minimal footprint.
- **Connection Isolation**: Concurrent HTTP requests instantiate thread-safe, isolated MySQL connections (`DatabaseManager`) avoiding driver contention while coordinating through the shared in-memory `LockManager` (2PL).
- **Uniform Envelope**: All responses adhere to a consistent JSON format with explicit success/error structures.
- **Informative Status Codes**: Utilizes HTTP `200`, `204`, `400`, `404`, `409`, `422`, `500`, and `503` rather than masking failures under generic status codes.

---

## 2. Server Configuration and Startup

### Configuration Variables
The server is configured via environment variables or CLI flags (with sensible local defaults):

| Variable | CLI Flag | Default | Description |
| :--- | :--- | :--- | :--- |
| `PAYLOCK_HOST` | `--host`, `-h` | `127.0.0.1` | Local loopback address for the HTTP listener |
| `PAYLOCK_PORT` | `--port`, `-p` | `8080` | TCP listening port |
| `DB_HOST` | - | `127.0.0.1` | MySQL server host |
| `DB_PORT` | - | `3306` | MySQL port |
| `DB_USER` | - | `root` | Database username |
| `DB_PASS` | - | `""` | Database password |
| `DB_NAME` | - | `paylock_db` | Database name |

### Startup Command
To start the REST API server:

```bash
# Using CLI flag
./paylock --server

# With custom port and host
./paylock --server --port 8080 --host 127.0.0.1

# Or run via interactive menu (Option 15)
./paylock -i
```

### Base URL
```
http://127.0.0.1:8080/api/v1
```

---

## 3. Standard Response Envelope

### Success Response (`200 OK` / `201 Created`)
```json
{
  "success": true,
  "data": { ... },
  "message": "Operation completed successfully"
}
```

### Error Response (`4xx` / `5xx`)
```json
{
  "success": false,
  "error": {
    "code": "ERROR_CODE",
    "message": "Human-readable diagnostic description"
  }
}
```

---

## 4. Endpoints Summary Table

| Category | Method | Path | Description | Status Codes |
| :--- | :--- | :--- | :--- | :--- |
| **System** | `GET` | `/api/v1/health` | Service health & MySQL connectivity probe | `200`, `503` |
| **System** | `GET` | `/api/v1/system/stats` | Aggregated metrics (Tx, Locks, Resources, Paging) | `200`, `500` |
| **Accounts** | `GET` | `/api/v1/accounts` | Paginated account list from MySQL | `200`, `500` |
| **Accounts** | `GET` | `/api/v1/accounts/:id` | Single account details & balance | `200`, `400`, `404` |
| **Transactions** | `GET` | `/api/v1/transactions` | Filtered & paginated transaction history | `200`, `500` |
| **Transactions** | `GET` | `/api/v1/transactions/:id`| Transaction details by ID | `200`, `400`, `404` |
| **Transactions** | `POST` | `/api/v1/transactions/transfer` | Execute atomic 2PL transfer | `200`, `400`, `404`, `409`, `422` |
| **Transactions** | `POST` | `/api/v1/transactions/deposit` | Credit account balance | `200`, `400`, `404`, `422` |
| **Transactions** | `POST` | `/api/v1/transactions/withdraw` | Debit account balance | `200`, `400`, `404`, `409`, `422` |
| **Concurrency** | `GET` | `/api/v1/concurrency/status` | Active worker pool & thread states | `200` |
| **Concurrency** | `GET` | `/api/v1/concurrency/transactions` | Worker execution and task queue status | `200` |
| **Locks** | `GET` | `/api/v1/locks` | Lock table snapshot (holders & wait queues) | `200` |
| **Locks** | `GET` | `/api/v1/locks/wait-for-graph`| Wait-For Graph (vertices, edges, cycles) | `200` |
| **Deadlocks** | `GET` | `/api/v1/deadlocks` | Cycle analysis & deadlock detection report | `200` |
| **Recovery** | `GET` | `/api/v1/recovery/history` | Historical deadlock abort & rollback records | `200` |
| **Resources** | `GET` | `/api/v1/resources` | Logical resource manager capacity & allocations | `200` |
| **Paging** | `GET` | `/api/v1/paging/stats` | Simulated OS LRU paging hit/fault metrics | `200` |
| **Paging** | `GET` | `/api/v1/paging/frames` | Simulated frame table & page mappings | `200` |

---

## 5. Detailed Endpoint Documentation

### 5.1 Health and System

#### `GET /api/v1/health`
Checks API server availability and validates active connection to MySQL InnoDB engine.

- **Sample Request**:
  ```bash
  curl -s http://127.0.0.1:8080/api/v1/health | jq .
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "PayLock API Server is healthy",
    "data": {
      "status": "healthy",
      "server": "healthy",
      "version": "1.0.0",
      "database": {
        "connected": true,
        "database": "paylock_db",
        "host": "127.0.0.1",
        "port": 3306,
        "status": "UP",
        "type": "MySQL 9.x (InnoDB)"
      },
      "services": {
        "concurrency_engine": "READY",
        "deadlock_detector": "READY",
        "lock_manager": "READY",
        "paging_simulator": "READY",
        "recovery_manager": "READY",
        "resource_manager": "READY",
        "transaction_engine": "READY"
      }
    }
  }
  ```

#### `GET /api/v1/system/stats`
Returns aggregated system-wide operational metrics across transactions, accounts, lock holders, deadlocks, and OS simulators.

- **Sample Request**:
  ```bash
  curl -s http://127.0.0.1:8080/api/v1/system/stats | jq .
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "System statistics retrieved successfully",
    "data": {
      "accounts": {
        "system_total_balance": 22000.0,
        "total_accounts": 3
      },
      "deadlocks": {
        "total_recoveries": 0
      },
      "locks": {
        "active_holders": 0,
        "waiting_requests": 0
      },
      "paging_simulation": {
        "fault_ratio": 0.0,
        "hit_ratio": 0.0,
        "page_faults": 0,
        "page_hits": 0,
        "total_references": 0
      },
      "resources": {
        "total_allocated": 0,
        "total_available": 1144
      },
      "transactions": {
        "committed": 12,
        "failed": 2,
        "pending": 0,
        "rolled_back": 1,
        "total": 15
      }
    }
  }
  ```

---

### 5.2 Accounts

#### `GET /api/v1/accounts`
Retrieves account list with pagination support (`limit`, `offset`, `page`).

- **Query Parameters**:
  - `limit`: Number of records to return (1 - 100, default `10`)
  - `offset`: Zero-indexed record offset (default `0`)
  - `page`: One-indexed page number (default `1`)
- **Sample Request**:
  ```bash
  curl -s "http://127.0.0.1:8080/api/v1/accounts?limit=2&offset=0" | jq .
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "Accounts retrieved successfully",
    "data": {
      "accounts": [
        {
          "account_id": 101,
          "balance": 9400.0,
          "created_at": "2026-10-13 14:00:00",
          "owner_name": "Alice Sharma",
          "user_id": 1
        },
        {
          "account_id": 102,
          "balance": 5600.0,
          "created_at": "2026-10-13 14:00:00",
          "owner_name": "Bob Verma",
          "user_id": 2
        }
      ],
      "count": 3,
      "limit": 2,
      "offset": 0,
      "total": 3,
      "pagination": {
        "limit": 2,
        "offset": 0,
        "page": 1,
        "total": 3,
        "total_pages": 2
      }
    }
  }
  ```

#### `GET /api/v1/accounts/{account_id}`
Retrieves a specific account. Accepts integer IDs (`101`) or formatted string IDs (`A101`).

- **Sample Request**:
  ```bash
  curl -s http://127.0.0.1:8080/api/v1/accounts/101 | jq .
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "Account details retrieved",
    "data": {
      "account_id": 101,
      "balance": 9400.0,
      "created_at": "2026-10-13 14:00:00",
      "owner_name": "Alice Sharma",
      "user_id": 1
    }
  }
  ```
- **Error Response (`404 Not Found`)**:
  ```json
  {
    "success": false,
    "error": {
      "code": "ACCOUNT_NOT_FOUND",
      "message": "Account with ID 9999 was not found"
    }
  }
  ```

---

### 5.3 Transactions

#### `POST /api/v1/transactions/transfer`
Executes an atomic transfer between two accounts using the backend's Two-Phase Locking (2PL) Lock Manager, MySQL ACID transaction engine, and persistent database-backed idempotency.

- **Request Headers**:
  - `Content-Type: application/json`
  - `Idempotency-Key`: *(Optional)* Client-generated idempotency key (1-128 printable ASCII characters without whitespace). Guarantees at-most-once execution across network retries.
- **Response Headers**:
  - `Idempotent-Replayed: true`: Present when the response was replayed from a previously executed request with the same idempotency key.
- **Request Body**:
  ```json
  {
    "from_account": 101,
    "to_account": 102,
    "amount": 500.00
  }
  ```
- **Validation Rules**:
  - `Idempotency-Key`: Must be non-empty ASCII string (1-128 chars without whitespace) if provided (`400 Bad Request`, code `INVALID_IDEMPOTENCY_KEY`)
  - Key reuse with different payload/operation: (`409 Conflict`, code `IDEMPOTENCY_CONFLICT`)
  - Key currently in-flight by concurrent request: waits up to 2.5s, then returns (`409 Conflict`, code `CONCURRENT_REQUEST_IN_FLIGHT`)
  - Required fields: `from_account`, `to_account`, `amount` (`400 Bad Request` if missing)
  - `amount > 0` and within allowed numeric range (`422 Unprocessable Entity`)
  - `from_account != to_account` (`422 Unprocessable Entity`, code `SELF_TRANSFER`)
  - Both accounts must exist in database (`404 Not Found`)
  - Sufficient balance available; overdrafts trigger safe MySQL rollback (`409 Conflict`, code `INSUFFICIENT_FUNDS` / `TRANSFER_FAILED`)
- **Sample Request**:
  ```bash
  curl -s -X POST http://127.0.0.1:8080/api/v1/transactions/transfer \
    -H "Content-Type: application/json" \
    -H "Idempotency-Key: paylock-transfer-101-102-tx99" \
    -d '{"from_account": 101, "to_account": 102, "amount": 500.00}' | jq .
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "Transfer completed and committed successfully",
    "data": {
      "transaction_id": 1016,
      "from_account": 101,
      "to_account": 102,
      "amount": 500.0,
      "status": "COMMITTED"
    }
  }
  ```
- **Overdraft Failure (`409 Conflict`)**:
  ```json
  {
    "success": false,
    "error": {
      "code": "TRANSFER_FAILED",
      "message": "Insufficient funds in Account 101 (Available: ₹9400.00, Requested: ₹999999.00)"
    }
  }
  ```

#### `POST /api/v1/transactions/deposit`
Credits funds into an existing account with persistent idempotency support.

- **Request Headers**:
  - `Content-Type: application/json`
  - `Idempotency-Key`: *(Optional)* Client-generated idempotency key
- **Request Body**:
  ```json
  {
    "account_id": 103,
    "amount": 300.00
  }
  ```

#### `POST /api/v1/transactions/withdraw`
Debits funds from an existing account with persistent idempotency support.

- **Request Headers**:
  - `Content-Type: application/json`
  - `Idempotency-Key`: *(Optional)* Client-generated idempotency key
- **Request Body**:
  ```json
  {
    "account_id": 103,
    "amount": 200.00
  }
  ```

#### `GET /api/v1/transactions`
Retrieves transaction history with optional status and account filtering.

- **Query Parameters**:
  - `status`: Optional filter (`COMMITTED`, `ROLLED_BACK`, `FAILED`)
  - `account_id`: Optional filter by participating account
  - `limit`: Number of records (default `10`)
  - `offset`: Zero-indexed offset (default `0`)
- **Sample Request**:
  ```bash
  curl -s "http://127.0.0.1:8080/api/v1/transactions?limit=5" | jq .
  ```

#### `GET /api/v1/transactions/{transaction_id}`
Retrieves details for a single persisted transaction.

- **Sample Request**:
  ```bash
  curl -s http://127.0.0.1:8080/api/v1/transactions/1016 | jq .
  ```

---

### 5.4 Concurrency Monitoring

#### `GET /api/v1/concurrency/status`
Returns real-time worker thread pool statuses, active tasks, and completion counts.

- **Sample Request**:
  ```bash
  curl -s http://127.0.0.1:8080/api/v1/concurrency/status | jq .
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "Concurrency status retrieved",
    "data": {
      "active_workers": 4,
      "is_running": true,
      "pool_size": 4,
      "worker_count": 4,
      "workers": [
        {
          "worker_id": 1,
          "state": "IDLE",
          "current_task": "None",
          "tasks_completed": 12
        }
      ]
    }
  }
  ```

---

### 5.5 Locking and Deadlocks

#### `GET /api/v1/locks`
Provides a consistent point-in-time snapshot of the in-memory Two-Phase Locking (2PL) Lock Manager table, including granted lock holders and FIFO wait queues.

- **Sample Request**:
  ```bash
  curl -s http://127.0.0.1:8080/api/v1/locks | jq .
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "Lock table snapshot retrieved",
    "data": {
      "active_transactions": 0,
      "total_locks": 0,
      "locks": [],
      "resources": [],
      "transaction_holdings": {}
    }
  }
  ```

#### `GET /api/v1/locks/wait-for-graph`
Returns the Wait-For Graph (WFG) computed from active lock waiting queues. Directed edges point from waiting transactions to holding transactions ($T_i \to T_j$).

- **Sample Request**:
  ```bash
  curl -s http://127.0.0.1:8080/api/v1/locks/wait-for-graph | jq .
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "Wait-For Graph retrieved",
    "data": {
      "vertices": ["T101", "T102"],
      "edges": [
        {
          "from": "T101",
          "to": "T102",
          "description": "T101 waits for T102"
        }
      ],
      "has_cycle": false,
      "deadlock_detected": false,
      "cycles": []
    }
  }
  ```

#### `GET /api/v1/deadlocks`
Executes Tarjan's cycle detection algorithm across the WFG to identify circular dependencies.

- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "Deadlock detection report retrieved",
    "data": {
      "deadlock_detected": false,
      "deadlock_present": false,
      "cycle_count": 0,
      "cycles": [],
      "participating_transactions": []
    }
  }
  ```

#### `GET /api/v1/recovery/history`
Returns chronological records of victim selection, transaction aborts, database rollbacks, and released locks executed by the `RecoveryManager`.

---

### 5.6 Resource Management and Paging Simulation

#### `GET /api/v1/resources`
Returns tracked allocations, capacities, and availability across logical system resources (`ACCOUNT_RECORD`, `TRANSACTION_SLOT`, `WORKER_CAPACITY`, `MEMORY_FRAME`).

- **Sample Request**:
  ```bash
  curl -s http://127.0.0.1:8080/api/v1/resources | jq .
  ```

#### `GET /api/v1/paging/stats`
Returns metrics from the educational LRU paging simulator.
*Note: This endpoint models OS virtual memory concepts and is decoupled from the MySQL InnoDB buffer pool.*

- **Sample Request**:
  ```bash
  curl -s http://127.0.0.1:8080/api/v1/paging/stats | jq .
  ```
- **Response (`200 OK`)**:
  ```json
  {
    "success": true,
    "message": "Paging simulation statistics retrieved",
    "data": {
      "is_simulation": true,
      "is_simulated_os_concept": true,
      "total_references": 20,
      "page_hits": 8,
      "page_faults": 12,
      "page_replacements": 9,
      "hit_ratio": 40.0,
      "fault_ratio": 60.0,
      "frame_count": 3,
      "occupied_frames": 3
    }
  }
  ```

#### `GET /api/v1/paging/frames`
Returns the current simulator frame table and page-to-frame mappings.

---

## 6. Security and Operational Safeguards

1. **Localhost Binding**: Binds by default to `127.0.0.1` preventing inadvertent external exposure.
2. **Credential Sanitization**: No SQL statements, raw error stacks, connection strings, or database credentials are leaked in HTTP responses.
3. **CORS Policy**: Standard CORS pre-flight (`OPTIONS 204`) with `Access-Control-Allow-Origin: *` configured for local frontend development (Google Stitch integration).
4. **Parameter Sanitization**: All database interactions use parameterization or rigorous string escaping via `DatabaseManager`.
5. **No Force Abort**: Administrative force-abort endpoints are intentionally prohibited to protect transaction engine consistency.

---

## 7. Automated Testing Suite

PayLock includes a dedicated integration test suite for the API layer in `tests/test_api.cpp`.

```bash
# Build and run API tests
cmake --build build --target test_api
./build/test_api

# Or execute full test suite via Makefile
make test
```

All 33 API integration tests verify HTTP response codes, JSON schemas, 2PL lock table states, and MySQL database balance conservation.
