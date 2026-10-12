# PayLock API Layer Specification

## Overview
The API layer defines the contract between the frontend dashboard and the C++ transaction engine. It will be implemented in future phases using a lightweight embedded C++ HTTP server (e.g. Crow / cpp-httplib).

---

## Planned Endpoints

### 1. `POST /api/transfer`
Initiates an atomic peer-to-peer transfer between accounts.
- **Request Body**:
  ```json
  {
    "sender_account": 101,
    "receiver_account": 102,
    "amount": 2000.00
  }
  ```
- **Response**:
  ```json
  {
    "status": "COMMITTED",
    "transaction_id": 1001,
    "message": "Transfer successfully completed."
  }
  ```

### 2. `POST /api/deposit`
Deposits funds into an account.
- **Request Body**:
  ```json
  {
    "account_id": 102,
    "amount": 1000.00
  }
  ```

### 3. `POST /api/withdraw`
Withdraws funds from an account.
- **Request Body**:
  ```json
  {
    "account_id": 103,
    "amount": 2000.00
  }
  ```

### 4. `GET /api/accounts`
Retrieves all account balances and owner names.

### 5. `GET /api/transactions`
Retrieves transaction history.

### 6. `GET /api/concurrency/status`
Returns live thread pool states, active locks, and WFG edges for visual dashboard rendering.
