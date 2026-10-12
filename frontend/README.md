# PayLock Frontend & Visual Dashboard

This directory contains the lightweight web-based visualization dashboard for PayLock.

## Overview

The dashboard visualizes:
1. **Real-time Account Balances**: Displays customer accounts (IDs 101, 102, 103) and their current balance state.
2. **Transaction Simulation**: Allows triggering simulated atomic transfers between accounts.
3. **Live Concurrency & Lock Monitor**: Visualizes worker threads, their current execution states, resource locks acquired/released, and thread-safe queue metrics.
4. **Transaction Log Stream**: Real-time event log reflecting DBMS transaction states (`COMMITTED`, `ROLLED_BACK`) and lock telemetry.

## Directory Structure

```
frontend/
├── index.html        # Main dashboard UI structure
├── css/
│   └── style.css     # Modern dark-mode styling with glassmorphism
├── js/
│   └── app.js        # Dynamic mock engine and future REST API client integration
└── README.md         # Frontend documentation
```

## Running the Dashboard Prototype

Since the dashboard uses vanilla HTML5, CSS3, and JavaScript, it can be viewed directly in any modern browser without external tooling:

```bash
# Option 1: Direct browser open
open frontend/index.html

# Option 2: Python simple HTTP server
python3 -m http.server 8080 --directory frontend
```

## Future API Integration

In upcoming phases, an embedded REST API layer (using a lightweight C++ HTTP server like `httplib` or `Crow`) will expose endpoints (`/api/accounts`, `/api/transactions`, `/api/locks`, `/api/concurrency`), which `frontend/js/app.js` will query via standard `fetch()` calls to display live backend metrics.
