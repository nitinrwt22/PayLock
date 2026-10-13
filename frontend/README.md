# PayLock Frontend Dashboard (Google Stitch Integration)

This directory contains the completed Single Page Application (SPA) dashboard for **PayLock**, built from the **Google Stitch** design system (*High-Velocity Transaction Protocol*).

---

## Overview

The dashboard interfaces directly with the embedded C++ REST API (`http://127.0.0.1:8080/api/v1`) and provides deep visual telemetry into:

1. **System Overview & Command Center**: 8-KPI bento metric strip, 2PL mutex contention map, real-time transaction ingestion ledger, and MySQL account states.
2. **Transactions Monitor**: Full search, state filtering pills (`All`, `Committed`, `Processing`, `Waiting`, `Rolled Back`, `Failed / Deadlock`), and dense data table.
3. **Transaction Inspector**: Microsecond latency execution breakdown (Queue, Mutex, SQL, WAL), ACID verification badges, lock timeline, and balance state diffs.
4. **Concurrency Monitor & Thread Pool**: Interactive POSIX thread pool pipeline visualization (vector flow board), active 2PL lock table, Wait-For Graph (WFG) cycle detection, and deadlock recovery history.
5. **OS Resources & Paging**: Logical resource quota monitoring and simulated 64-frame memory page table with LRU hit/fault metrics.
6. **ACID Transfer Modal**: Interactive transfer dialog connected directly to `POST /api/v1/transactions/transfer` with real-time balance updates and error reporting.

---

## Directory Structure

```text
frontend/
├── index.html          # Master SPA implementing all 4 Stitch screens
├── css/
│   └── style.css       # Design tokens, custom animations, toasts, and modal backdrops
├── js/
│   ├── api.js          # Centralized C++ REST API client (http://127.0.0.1:8080/api/v1)
│   ├── app.js          # SPA navigation, hotkeys (⌘1-6, N), telemetry polling, modal controller
│   └── mock-data.js    # Fallback telemetry matching Stitch specification
├── design/
│   ├── DESIGN.md       # Color tokens, typography, elevation, and layout geometry
│   ├── SCREEN_INVENTORY.md # 4 Stitch screens, IDs, references, and endpoint bindings
│   └── reference/      # Archived Stitch HTML source files and reference PNG screenshots
└── README.md           # This file
```

---

## Design System & Tokens

- **Design Origin**: Google Stitch (Project ID: `14592152822615229882`)
- **Typography**: `Geist` (prose and headings) and `JetBrains Mono` (hashes, numbers, currency, badges)
- **Palette**: Dark engineered theme:
  - Surface: `#111318` (Canvas), `#0C0E13` (Sidebar), `#1A1B21` (Cards)
  - Accents: `#74EF00` (Primary neon), `#59DE9B` (Secondary), `#DCCCFF` (Contention), `#FFB4AB` (Deadlock)
- **Geometry**: Fixed 256px sidebar, 56px top header with breadcrumb navigation.

---

## Running the Dashboard

### 1. Start the C++ REST API Server
```bash
./build/paylock --server --port 8080 --host 127.0.0.1
```

### 2. Serve the Frontend
```bash
python3 -m http.server 3000 --directory frontend
```
Open **`http://localhost:3000`** in any web browser.

### Keyboard Shortcuts
- `⌘1`: Overview Dashboard
- `⌘2`: Transactions Monitor
- `⌘3`: Transaction Inspector
- `⌘4`: Accounts View
- `⌘5`: Thread Pool / Concurrency View
- `⌘6`: Lock Manager
- `N`: Open New Transfer Modal
- `⌘K`: Focus Global Search Input
- `Escape`: Close Active Modal
