/**
 * PayLock — Realistic Mock Data Provider
 * Matches Stitch Reference Telemetry & provides graceful offline fallback
 */
const MockData = {
  systemStats: {
    transactions: {
      total: 12482,
      committed: 12210,
      rolled_back: 272,
      pending: 14,
      failed: 54
    },
    concurrency: {
      active_threads: 18,
      running_threads: 15,
      waiting_threads: 3,
      pool_size: 24,
      tps: 4218,
      avg_latency_ms: 12.4
    },
    locks: {
      active_holders: 7,
      exclusive_locks: 5,
      shared_locks: 2,
      waiting_requests: 2
    },
    deadlocks: {
      detected: 2,
      resolved: 1,
      cycles_active: 1
    },
    paging: {
      frames_total: 64,
      frames_used: 42,
      page_hits: 18450,
      page_faults: 342,
      hit_ratio: 0.9818
    },
    resources: {
      cpu_cores: 8,
      total_memory_mb: 4096,
      used_memory_mb: 1420
    }
  },

  accounts: [
    { account_id: 101, owner_name: "Alice Sharma", user_id: 1, balance: 9400.00, created_at: "2026-10-13 14:00:00", status: "ACTIVE" },
    { account_id: 102, owner_name: "Bob Verma", user_id: 2, balance: 5600.00, created_at: "2026-10-13 14:00:00", status: "ACTIVE" },
    { account_id: 103, owner_name: "Charlie Singh", user_id: 3, balance: 7000.00, created_at: "2026-10-13 14:00:00", status: "ACTIVE" },
    { account_id: 104, owner_name: "PayLock Treasury", user_id: 4, balance: 125000.00, created_at: "2026-10-13 14:00:00", status: "ACTIVE" },
    { account_id: 105, owner_name: "Dev Escrow", user_id: 5, balance: 18450.50, created_at: "2026-10-13 14:00:00", status: "ACTIVE" }
  ],

  transactions: [
    {
      transaction_id: 10485,
      id_str: "TXN-10485",
      from_account_id: 101,
      to_account_id: 102,
      amount: 1250.00,
      status: "COMMITTED",
      lock_mode: "EXCLUSIVE",
      worker_id: "T-01",
      latency_ms: 18,
      timestamp: "10:42:18.420",
      created_at: "2026-10-13 10:42:18",
      isolation: "SERIALIZABLE (2PL)"
    },
    {
      transaction_id: 10484,
      id_str: "TXN-10484",
      from_account_id: 102,
      to_account_id: 103,
      amount: 450.00,
      status: "COMMITTED",
      lock_mode: "EXCLUSIVE",
      worker_id: "T-03",
      latency_ms: 12,
      timestamp: "10:42:15.118",
      created_at: "2026-10-13 10:42:15",
      isolation: "SERIALIZABLE (2PL)"
    },
    {
      transaction_id: 10483,
      id_str: "TXN-10483",
      from_account_id: 103,
      to_account_id: 101,
      amount: 820.00,
      status: "PROCESSING",
      lock_mode: "EXCLUSIVE",
      worker_id: "T-02",
      latency_ms: 24,
      timestamp: "10:42:12.890",
      created_at: "2026-10-13 10:42:12",
      isolation: "SERIALIZABLE (2PL)"
    },
    {
      transaction_id: 10482,
      id_str: "TXN-10482",
      from_account_id: 101,
      to_account_id: 104,
      amount: 3500.00,
      status: "WAITING",
      lock_mode: "EXCLUSIVE",
      worker_id: "T-05",
      latency_ms: 145,
      timestamp: "10:42:09.004",
      created_at: "2026-10-13 10:42:09",
      isolation: "SERIALIZABLE (2PL)"
    },
    {
      transaction_id: 10481,
      id_str: "TXN-10481",
      from_account_id: 102,
      to_account_id: 101,
      amount: 5000.00,
      status: "ROLLED_BACK",
      lock_mode: "EXCLUSIVE",
      worker_id: "T-04",
      latency_ms: 95,
      timestamp: "10:42:01.321",
      created_at: "2026-10-13 10:42:01",
      isolation: "SERIALIZABLE (2PL)"
    },
    {
      transaction_id: 10480,
      id_str: "TXN-10480",
      from_account_id: 103,
      to_account_id: 102,
      amount: 9500.00,
      status: "DEADLOCK",
      lock_mode: "EXCLUSIVE",
      worker_id: "T-06",
      latency_ms: 240,
      timestamp: "10:41:45.912",
      created_at: "2026-10-13 10:41:45",
      isolation: "SERIALIZABLE (2PL)"
    },
    {
      transaction_id: 10479,
      id_str: "TXN-10479",
      from_account_id: 101,
      to_account_id: 103,
      amount: 150.00,
      status: "COMMITTED",
      lock_mode: "SHARED",
      worker_id: "T-01",
      latency_ms: 9,
      timestamp: "10:41:30.120",
      created_at: "2026-10-13 10:41:30",
      isolation: "SERIALIZABLE (2PL)"
    },
    {
      transaction_id: 10478,
      id_str: "TXN-10478",
      from_account_id: 104,
      to_account_id: 101,
      amount: 2200.00,
      status: "COMMITTED",
      lock_mode: "EXCLUSIVE",
      worker_id: "T-07",
      latency_ms: 14,
      timestamp: "10:41:14.650",
      created_at: "2026-10-13 10:41:14",
      isolation: "SERIALIZABLE (2PL)"
    }
  ],

  concurrency: {
    workers: [
      { id: "T-01", name: "Worker T-01", state: "RUNNING", current_txn: 10485, stage: "DB Balance Write", latency: "14ms" },
      { id: "T-02", name: "Worker T-02", state: "RUNNING", current_txn: 10483, stage: "ACID Validation", latency: "22ms" },
      { id: "T-03", name: "Worker T-03", state: "IDLE", current_txn: null, stage: "Awaiting Dispatch", latency: "0ms" },
      { id: "T-04", name: "Worker T-04", state: "MUTEX_WAIT", current_txn: 10482, stage: "Waiting Lock ACC-101", latency: "145ms" },
      { id: "T-05", name: "Worker T-05", state: "COMMITTED", current_txn: 10484, stage: "Post-Commit Flush", latency: "8ms" },
      { id: "T-06", name: "Worker T-06", state: "CYCLE_DETECTED", current_txn: 10480, stage: "Deadlock Abort/Rollback", latency: "240ms" },
      { id: "T-07", name: "Worker T-07", state: "IDLE", current_txn: null, stage: "Awaiting Dispatch", latency: "0ms" },
      { id: "T-08", name: "Worker T-08", state: "IDLE", current_txn: null, stage: "Awaiting Dispatch", latency: "0ms" }
    ],
    locks: [
      { resource: "ACC-101", mode: "EXCLUSIVE", holder_txn: 10485, waiting_txns: [10482], acquired_at: "10:42:18.420" },
      { resource: "ACC-102", mode: "EXCLUSIVE", holder_txn: 10485, waiting_txns: [], acquired_at: "10:42:18.422" },
      { resource: "ACC-103", mode: "SHARED", holder_txn: 10483, waiting_txns: [], acquired_at: "10:42:12.890" },
      { resource: "ACC-104", mode: "EXCLUSIVE", holder_txn: 10482, waiting_txns: [], acquired_at: "10:42:09.004" }
    ],
    wfg: {
      vertices: [10480, 10482, 10483, 10485],
      edges: [
        { from: 10482, to: 10485, resource: "ACC-101" },
        { from: 10480, to: 10482, resource: "ACC-104" },
        { from: 10485, to: 10480, resource: "ACC-103" }
      ],
      has_cycle: true,
      cycle: [10480, 10482, 10485, 10480],
      selected_victim: 10480,
      recovery_state: "VICTIM_SELECTED_ABORTING"
    },
    recovery_history: [
      {
        id: "REC-892",
        timestamp: "10:41:45.920",
        victim_txn: 10480,
        cycle: "10480 -> 10482 -> 10485 -> 10480",
        action: "ABORT_ROLLBACK",
        locks_released: ["ACC-103 Mutex"],
        status: "COMPLETED"
      },
      {
        id: "REC-891",
        timestamp: "09:14:22.105",
        victim_txn: 10321,
        cycle: "10321 -> 10322 -> 10321",
        action: "ABORT_ROLLBACK",
        locks_released: ["ACC-102 Mutex"],
        status: "COMPLETED"
      }
    ]
  },

  detailSample: {
    transaction_id: 10485,
    id_str: "TXN-10485",
    hash: "0x7f9a2b84c01d9f8e331b2c4e",
    status: "COMMITTED",
    from_account: 101,
    from_name: "Alice Sharma",
    to_account: 102,
    to_name: "Bob Verma",
    amount: 1250.00,
    timestamp: "2026-10-13 10:42:18.420 UTC",
    worker_thread: "Worker T-01 (pthread_t: 0x70000a12)",
    isolation: "SERIALIZABLE (Strict 2PL Engine)",
    latency_total_ms: 124,
    latency_breakdown: {
      queue_ms: 12,
      mutex_wait_ms: 4,
      execution_ms: 88,
      wal_flush_ms: 20
    },
    acid_checks: {
      atomicity: "PASSED — All WAL records synchronized",
      consistency: "PASSED — System sum invariant verified ($22,000.00)",
      isolation: "PASSED — Strict 2PL exclusive locks held until commit",
      durability: "PASSED — InnoDB redo log fsynced to disk"
    },
    locks: [
      { id: "LOCK-21", resource: "ACC-101 Mutex", mode: "EXCLUSIVE", acquired: "10:42:18.420", released: "10:42:18.544" },
      { id: "LOCK-22", resource: "ACC-102 Mutex", mode: "EXCLUSIVE", acquired: "10:42:18.422", released: "10:42:18.544" }
    ],
    balance_diff: [
      { account: "ACC-101 (Alice)", before: 10650.00, after: 9400.00, diff: -1250.00 },
      { account: "ACC-102 (Bob)", before: 4350.00, after: 5600.00, diff: +1250.00 }
    ],
    sql_log: [
      "10:42:18.420 [BEGIN TRANSACTION ID=10485]",
      "10:42:18.421 [ACQUIRE EXCLUSIVE LOCK ON ACC-101] -> GRANTED",
      "10:42:18.422 [ACQUIRE EXCLUSIVE LOCK ON ACC-102] -> GRANTED",
      "10:42:18.435 [UPDATE accounts SET balance = balance - 1250.00 WHERE account_id = 101] -> 1 row affected",
      "10:42:18.470 [UPDATE accounts SET balance = balance + 1250.00 WHERE account_id = 102] -> 1 row affected",
      "10:42:18.510 [INSERT INTO transactions (from_acc, to_acc, amount, status) VALUES (101, 102, 1250.00, 'COMMITTED')]",
      "10:42:18.540 [COMMIT TRANSACTION ID=10485] -> WAL FSYNC SUCCESS",
      "10:42:18.544 [RELEASE ALL LOCKS FOR TXN 10485] -> LOCK-21, LOCK-22 FREED"
    ]
  }
};

if (typeof window !== 'undefined') {
  window.MockData = MockData;
}
