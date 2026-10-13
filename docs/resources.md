# PayLock Resource Management & Paging Simulation Specification (Step 7)

## 1. Overview and Motivation

**PayLock** integrates Operating Systems (OS) concurrency and memory management concepts with Database Management Systems (DBMS) transactional guarantees. 

While earlier steps established:
- ACID Transaction processing (Step 2)
- Multithreading and worker execution pools (Step 3)
- Granular 2-Phase Locking (2PL) (Step 4)
- Wait-For Graph (WFG) cycle detection (Step 5)
- Deadlock recovery through cooperative transaction rollback (Step 6)

**Step 7** introduces **Resource Management** and an educational **Paging Simulation**:
1. **Logical Resource Management (`paylock::ResourceManager`)**: Tracks system resource capacity limits, handles admission control, prevents resource exhaustion/starvation, and tracks per-owner allocations without negative counters.
2. **Paging Simulator (`paylock::PagingSimulator`)**: Demonstrates fundamental virtual memory concepts—virtual pages, physical frames, page tables, page faults, page hits, and Least Recently Used (LRU) page replacement.

---

## 2. Resource Manager vs. Lock Manager

A critical distinction in PayLock's architecture is the difference between **Logical System Resources** and **Database/Application Locks**:

| Dimension | Logical Resource (`ResourceManager`) | Concurrency Lock (`LockManager`) |
|---|---|---|
| **Purpose** | Capacity tracking, admission control, quota throttling | Mutual exclusion, isolation, serialized data access |
| **Entities Managed** | Numerical capacities (`ACCOUNT_RECORDS`, `TRANSACTION_SLOTS`, `WORKER_CAPACITY`, `MEMORY_FRAMES`) | Specific business rows (`ACCOUNT:A101`, `ACCOUNT:A102`) |
| **Modes** | Quantitative units (Allocated vs. Available) | Qualitative modes (`SHARED` S vs. `EXCLUSIVE` X) |
| **Contention Model** | Numerical exhaustion (`getAvailableCapacity() == 0`) | Incompatible lock modes ($S+X$, $X+X$) |
| **Queue / Waiting** | Admission rejection (`ResourceAllocationResult::EXHAUSTED`) | Wait queue condition variable suspension (`waitQueue`) |
| **Release Trigger** | Transaction completion or worker de-allocation | Transaction `COMMIT` or `ROLLBACK` (2PL) |

---

## 3. Logical Resources Tracked

`ResourceManager` manages system capacities defined by `ResourceType`:

1. **`ACCOUNT_RECORDS` (`ResourceType::ACCOUNT_RECORD`)**:
   - Capacity of cached account ledger buffers in memory (default: 1,000 slots).
2. **`TRANSACTION_SLOTS` (`ResourceType::TRANSACTION_SLOT`)**:
   - System-wide admission ceiling for concurrently processing transactions (default: 64 slots).
3. **`WORKER_CAPACITY` (`ResourceType::WORKER_CAPACITY`)**:
   - Active worker execution permits across the background thread pool (default: 16 units).
4. **`MEMORY_FRAMES` (`ResourceType::MEMORY_FRAME`)**:
   - Simulated physical memory frames available for page allocation (default: 64 frames).

### Key Invariants Enforced:
- **Conservation of Capacity**: $\text{Total Capacity} = \text{Allocated Capacity} + \text{Available Capacity}$ at all times.
- **Underflow Protection**: Releasing more units than currently allocated is safely rejected with `false`; counters never become negative.
- **Per-Owner Cleanup**: `releaseAllForOwner(ownerId)` ensures that when a transaction unwinds or commits, all its reserved capacity is reclaimed atomically.
- **Thread Safety**: All state mutations are protected by `mutable std::mutex resourceMutex_` using RAII lock guards.

---

## 4. Paging Simulation Concepts

The Paging Simulator model demonstrates how an operating system translates virtual memory requests into physical memory frames.

### Key Concepts:
- **Virtual Page**: A uniform block of logical address space requested by a program.
- **Physical Frame**: A fixed-size physical memory slot where a virtual page resides when loaded into RAM.
- **Page Table**: A mapping structure (`pageNumber -> frameIndex`) that tracks which virtual pages are currently resident in physical memory.
- **Page Hit**: Occurs when a requested virtual page is already resident in a physical frame. Access time is recorded, and no disk/secondary retrieval is required.
- **Page Fault**: Occurs when a requested virtual page is not currently in memory. The OS must load the page from secondary storage into a physical frame.
- **Empty Frame Loading**: If free frames exist, the faulted page is placed into the first free frame without evicting any resident pages.
- **Page Replacement**: When all frames are occupied and a page fault occurs, an existing resident page must be evicted to make room for the requested page.

---

## 5. Least Recently Used (LRU) Replacement Algorithm

PayLock implements the **Least Recently Used (LRU)** replacement policy via `LRUReplacementPolicy`:
- **Principle**: The page that has not been accessed for the longest period of time is chosen as the victim for eviction.
- **Mechanism**:
  - A logical clock timestamp (`currentTime_`) increments on every page reference.
  - On every **Page Hit** or **Page Load**, `recordAccess(pageNumber, currentTime_)` updates `lastAccessTime_[pageNumber]`.
  - When all frames are full, `selectVictim(frames)` inspects resident pages across all occupied frames and selects the frame holding the page with $\min(\text{lastAccessTime})$.
  - Ties are broken deterministically by selecting the lowest frame index.
- **Extensibility**: The simulator uses the `PageReplacementPolicy` interface, allowing other algorithms (FIFO, Optimal, Clock) to be added without modifying simulator logic.

---

## 6. Textbook Reference String Verification

PayLock's paging simulator is validated against the classic benchmark from **Operating System Concepts (Silberschatz, Galvin, Gagne)**:

- **Frames**: 3
- **Reference String**: `{ 7, 0, 1, 2, 0, 3, 0, 4, 2, 3, 0, 3, 2, 1, 2, 0, 1, 7, 0, 1 }` (20 references)

### Step-by-Step Trace:
```text
Step  1 | Page  7 -> [FAULT] | Frame:  0 | Evicted: None    | Frames: [ 7 | - | - ]
Step  2 | Page  0 -> [FAULT] | Frame:  1 | Evicted: None    | Frames: [ 7 | 0 | - ]
Step  3 | Page  1 -> [FAULT] | Frame:  2 | Evicted: None    | Frames: [ 7 | 0 | 1 ]
Step  4 | Page  2 -> [FAULT] | Frame:  0 | Evicted Page:  7 | Frames: [ 2 | 0 | 1 ]
Step  5 | Page  0 -> [HIT ]  | Frame:  1 | Evicted: None    | Frames: [ 2 | 0 | 1 ]
Step  6 | Page  3 -> [FAULT] | Frame:  2 | Evicted Page:  1 | Frames: [ 2 | 0 | 3 ]
Step  7 | Page  0 -> [HIT ]  | Frame:  1 | Evicted: None    | Frames: [ 2 | 0 | 3 ]
Step  8 | Page  4 -> [FAULT] | Frame:  0 | Evicted Page:  2 | Frames: [ 4 | 0 | 3 ]
Step  9 | Page  2 -> [FAULT] | Frame:  2 | Evicted Page:  3 | Frames: [ 4 | 0 | 2 ]
Step 10 | Page  3 -> [FAULT] | Frame:  1 | Evicted Page:  0 | Frames: [ 4 | 3 | 2 ]
Step 11 | Page  0 -> [FAULT] | Frame:  0 | Evicted Page:  4 | Frames: [ 0 | 3 | 2 ]
Step 12 | Page  3 -> [HIT ]  | Frame:  1 | Evicted: None    | Frames: [ 0 | 3 | 2 ]
Step 13 | Page  2 -> [HIT ]  | Frame:  2 | Evicted: None    | Frames: [ 0 | 3 | 2 ]
Step 14 | Page  1 -> [FAULT] | Frame:  0 | Evicted Page:  0 | Frames: [ 1 | 3 | 2 ]
Step 15 | Page  2 -> [HIT ]  | Frame:  2 | Evicted: None    | Frames: [ 1 | 3 | 2 ]
Step 16 | Page  0 -> [FAULT] | Frame:  1 | Evicted Page:  3 | Frames: [ 1 | 0 | 2 ]
Step 17 | Page  1 -> [HIT ]  | Frame:  0 | Evicted: None    | Frames: [ 1 | 0 | 2 ]
Step 18 | Page  7 -> [FAULT] | Frame:  2 | Evicted Page:  2 | Frames: [ 1 | 0 | 7 ]
Step 19 | Page  0 -> [HIT ]  | Frame:  1 | Evicted: None    | Frames: [ 1 | 0 | 7 ]
Step 20 | Page  1 -> [HIT ]  | Frame:  0 | Evicted: None    | Frames: [ 1 | 0 | 7 ]
```

### Verified Statistics:
- **Total References**: 20
- **Page Hits**: 8 ($\text{Hit Ratio} = 40.00\%$)
- **Page Faults**: 12 ($\text{Fault Ratio} = 60.00\%$)
- **Page Replacements**: 9
- **Final Resident Pages**: $\{1, 0, 7\}$

---

## 7. Educational Boundary & Limitations

It is essential to understand the architectural scope of this simulation:
1. **Educational Model**:
   - The simulator does **not** inspect or alter the host OS (macOS/Linux) virtual memory subsystem or page tables (`mmap`, page tables, TLB).
   - The simulator does **not** replace MySQL InnoDB's internal buffer pool memory management (`innodb_buffer_pool_size`).
2. **Payment Safety**:
   - A simulated page fault **never** causes a real payment transaction to fail or roll back.
   - Paging events never hold MySQL database connections or application-level locks.
   - Simulated resource tracking is strictly decoupled from ledger debit/credit correctness.

---

## 8. How to Run Demos and Tests

### Run Automated Unit & Regression Tests (All 89 Tests)
```bash
# Using make:
make test

# Using cmake:
cmake --build build && ./build/test_db
```

### Run the Interactive CLI Demonstration
```bash
./paylock --interactive
# Select Option 11 for Resource Management Demo
# Select Option 12 for LRU Paging Simulation Demo
```

### Run the Complete Automated Showcase
```bash
./paylock --auto
# Runs Steps 1 through 13 in an automated sequence
```
