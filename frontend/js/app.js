/**
 * PayLock — Frontend Application Controller
 * High-Velocity Transaction Protocol & Multi-Screen SPA Manager
 */

(function () {
  'use strict';

  // State Store
  const state = {
    currentView: 'overview',
    accounts: [],
    transactions: [],
    concurrency: null,
    systemStats: null,
    wfg: null,
    locks: [],
    recoveries: [],
    selectedTxn: null,
    activeFilter: 'All',
    searchQuery: '',
    startTime: Date.now() - (4 * 3600 + 12 * 60 + 44) * 1000 // default 04h 12m 44s
  };

  // DOM Elements Cache
  const elements = {};

  function initElements() {
    elements.views = document.querySelectorAll('.app-view');
    elements.navItems = document.querySelectorAll('.nav-item');
    elements.breadcrumb = document.getElementById('header-breadcrumb');
    elements.clusterText = document.getElementById('header-cluster-text');
    elements.clusterDot = document.getElementById('header-cluster-dot');
    elements.backendStatusDot = document.getElementById('backend-status-dot');
    elements.backendVersionChip = document.getElementById('backend-version-chip');
    elements.uptimeClock = document.getElementById('uptime-clock');
    elements.syncBtn = document.getElementById('sync-state-btn');
    elements.transferModal = document.getElementById('transfer-modal');
    elements.openModalBtn = document.getElementById('open-transfer-modal-btn');
    elements.closeModalBtn = document.getElementById('close-transfer-modal-btn');
    elements.cancelModalBtn = document.getElementById('modal-cancel-btn');
    elements.transferForm = document.getElementById('transfer-form');
    elements.modalAlert = document.getElementById('modal-alert-box');
    elements.modalSubmitBtn = document.getElementById('modal-submit-btn');
    elements.modalSubmitText = document.getElementById('modal-submit-text');
    elements.modalFromAccount = document.getElementById('modal-from-account');
    elements.modalToAccount = document.getElementById('modal-to-account');
    elements.modalAmount = document.getElementById('modal-amount');
    elements.toastContainer = document.getElementById('toast-container');
    elements.globalSearch = document.getElementById('global-search-input');
    elements.simulateBtn = document.getElementById('simulate-traffic-btn');

    // Overview Elements
    elements.overviewAccounts = document.getElementById('overview-accounts-list');
    elements.overviewTxns = document.getElementById('overview-txns-table');
    elements.kpiTotalVal = document.getElementById('kpi-total-val');
    elements.kpiCommittedVal = document.getElementById('kpi-committed-val');
    elements.kpiCommittedPct = document.getElementById('kpi-committed-pct');
    elements.kpiRollbackVal = document.getElementById('kpi-rollback-val');
    elements.kpiRollbackPct = document.getElementById('kpi-rollback-pct');
    elements.kpiThreadsVal = document.getElementById('kpi-threads-val');
    elements.kpiLocksVal = document.getElementById('kpi-locks-val');
    elements.kpiDeadlocksVal = document.getElementById('kpi-deadlocks-val');
    elements.kpiPagingVal = document.getElementById('kpi-paging-val');
    elements.kpiLatencyVal = document.getElementById('kpi-latency-val');
    elements.sideWorkersCount = document.getElementById('side-workers-count');
    elements.sideLocksCount = document.getElementById('side-locks-count');
    elements.sideIpcStatus = document.getElementById('side-ipc-status');
    elements.deadlockBadge = document.getElementById('deadlock-badge');

    // Transactions Table Elements
    elements.txnsTableBody = document.getElementById('txns-table-body');
    elements.txnsSearch = document.getElementById('txns-search-input');
    elements.statusPills = document.querySelectorAll('.status-pill');
    elements.tableShowingRange = document.getElementById('table-showing-range');
    elements.tableTotalCount = document.getElementById('table-total-count');

    // Concurrency Elements
    elements.threadPipelineBoard = document.getElementById('thread-pipeline-board');
    elements.locksTableBody = document.getElementById('locks-table-body');
    elements.activeLocksCount = document.getElementById('active-locks-count');
    elements.wfgEdgesList = document.getElementById('wfg-edges-list');
    elements.wfgVictimTxn = document.getElementById('wfg-victim-txn');
    elements.recoveryTableBody = document.getElementById('recovery-table-body');

    // Transaction Detail Elements
    elements.detailBreadcrumbId = document.getElementById('detail-breadcrumb-id');
    elements.detailTitle = document.getElementById('detail-title');
    elements.detailStatusBadge = document.getElementById('detail-status-badge');
    elements.detailHashString = document.getElementById('detail-hash-string');
    elements.detailTotalLatency = document.getElementById('detail-total-latency');
    elements.detailBalanceDiffs = document.getElementById('detail-balance-diffs');
    elements.detailSqlTrace = document.getElementById('detail-sql-trace');
    elements.detailBackBtn = document.getElementById('detail-back-btn');

    // Paging frames
    elements.framesGrid = document.getElementById('frames-grid');
  }

  // View Navigation
  function switchView(viewName) {
    if (!viewName) return;
    
    // Map navigation aliases to views
    const viewMap = {
      'overview': 'view-overview',
      'transactions': 'view-transactions',
      'transaction-detail': 'view-transaction-detail',
      'accounts': 'view-overview', // Accounts section highlighted inside Overview
      'concurrency': 'view-concurrency',
      'lock-manager': 'view-concurrency',
      'deadlock-monitor': 'view-concurrency',
      'resources': 'view-resources',
      'system-logs': 'view-concurrency',
      'analytics': 'view-overview',
      'system-health': 'view-overview'
    };

    const targetSectionId = viewMap[viewName] || 'view-overview';
    state.currentView = viewName;

    // Toggle active view sections
    elements.views.forEach(v => {
      if (v.id === targetSectionId) {
        v.classList.add('active-view');
      } else {
        v.classList.remove('active-view');
      }
    });

    // Update active nav styles
    elements.navItems.forEach(item => {
      const path = item.getAttribute('data-view') || item.getAttribute('href')?.replace('#', '');
      if (path === viewName) {
        item.className = 'nav-item flex items-center justify-between px-space-sm py-1.5 rounded-DEFAULT transition-colors bg-surface-container text-primary font-semibold border-l-2 border-primary';
      } else {
        item.className = 'nav-item flex items-center justify-between px-space-sm py-1.5 rounded-DEFAULT text-on-surface-variant hover:bg-surface-container hover:text-on-surface transition-colors';
      }
    });

    // Update breadcrumb
    const breadcrumbNames = {
      'overview': 'ENGINE-CORE-01 / OVERVIEW',
      'transactions': 'PIPELINE / TRANSACTIONS',
      'transaction-detail': 'INSPECTOR / TXN-LIFECYCLE',
      'accounts': 'STORAGE / MYSQL-ACCOUNTS',
      'concurrency': 'RUNTIME / THREAD-POOL',
      'lock-manager': 'CONCURRENCY / 2PL-LOCKS',
      'deadlock-monitor': 'CONCURRENCY / DEADLOCKS',
      'resources': 'OS / PAGING-AND-RESOURCES',
      'system-logs': 'RUNTIME / SYSTEM-LOGS',
      'analytics': 'ANALYTICS / TELEMETRY',
      'system-health': 'CLUSTER / HEALTH'
    };
    if (elements.breadcrumb) {
      elements.breadcrumb.textContent = breadcrumbNames[viewName] || viewName.toUpperCase();
    }

    window.scrollTo({ top: 0, behavior: 'smooth' });
  }

  // Toast Notification System
  function showToast(title, message, type = 'info') {
    if (!elements.toastContainer) return;
    const toast = document.createElement('div');
    toast.className = `toast toast-${type}`;
    
    const iconName = type === 'success' ? 'check_circle' : type === 'error' ? 'error' : 'info';
    const iconColor = type === 'success' ? 'text-primary' : type === 'error' ? 'text-error' : 'text-secondary';

    toast.innerHTML = `
      <span class="material-symbols-outlined ${iconColor} text-[20px]">${iconName}</span>
      <div class="flex-1 min-w-0">
        <div class="font-headline-sm text-body-sm font-semibold text-on-surface leading-tight">${title}</div>
        <div class="font-code-sm text-code-sm text-outline truncate">${message}</div>
      </div>
    `;

    elements.toastContainer.appendChild(toast);
    setTimeout(() => {
      toast.style.opacity = '0';
      toast.style.transform = 'translateY(10px)';
      setTimeout(() => toast.remove(), 250);
    }, 4000);
  }

  // Uptime Clock
  function updateUptimeClock() {
    if (!elements.uptimeClock) return;
    const elapsedSeconds = Math.floor((Date.now() - state.startTime) / 1000);
    const hrs = String(Math.floor(elapsedSeconds / 3600)).padStart(2, '0');
    const mins = String(Math.floor((elapsedSeconds % 3600) / 60)).padStart(2, '0');
    const secs = String(elapsedSeconds % 60).padStart(2, '0');
    elements.uptimeClock.textContent = `${hrs}h ${mins}m ${secs}s`;
  }

  // Format Currency
  function formatMoney(amount) {
    return '$' + Number(amount).toLocaleString('en-US', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
  }

  // Status Badge HTML Helper
  function getStatusBadge(status) {
    const s = String(status || '').toUpperCase();
    if (s === 'COMMITTED' || s === 'SUCCESS') {
      return `<span class="px-2 py-0.5 rounded-DEFAULT bg-primary/10 text-primary font-code-sm text-code-sm border border-primary/20 flex items-center gap-1 w-fit"><span class="w-1.5 h-1.5 rounded-full bg-primary"></span>COMMITTED</span>`;
    }
    if (s === 'PROCESSING' || s === 'PENDING') {
      return `<span class="px-2 py-0.5 rounded-DEFAULT bg-primary-container/20 text-primary-fixed-dim font-code-sm text-code-sm border border-primary-container/30 flex items-center gap-1 w-fit"><span class="w-1.5 h-1.5 rounded-full bg-primary-container animate-ping"></span>PROCESSING</span>`;
    }
    if (s === 'WAITING' || s === 'QUEUED') {
      return `<span class="px-2 py-0.5 rounded-DEFAULT bg-secondary-container/20 text-secondary font-code-sm text-code-sm border border-secondary/20 flex items-center gap-1 w-fit"><span class="w-1.5 h-1.5 rounded-full bg-secondary"></span>WAITING</span>`;
    }
    if (s === 'ROLLED_BACK' || s === 'ABORTED') {
      return `<span class="px-2 py-0.5 rounded-DEFAULT bg-error-container/30 text-error font-code-sm text-code-sm border border-error/20 flex items-center gap-1 w-fit"><span class="w-1.5 h-1.5 rounded-full bg-error"></span>ROLLED BACK</span>`;
    }
    if (s === 'DEADLOCK' || s === 'FAILED') {
      return `<span class="px-2 py-0.5 rounded-DEFAULT bg-error-container/50 text-error font-code-sm text-code-sm border border-error/30 flex items-center gap-1 w-fit"><span class="w-1.5 h-1.5 rounded-full bg-error animate-ping"></span>DEADLOCK</span>`;
    }
    return `<span class="px-2 py-0.5 rounded-DEFAULT bg-surface-container text-outline font-code-sm text-code-sm">${s}</span>`;
  }

  // Render Overview Accounts
  function renderAccounts(accounts) {
    state.accounts = accounts;
    if (!elements.overviewAccounts) return;

    if (!accounts || accounts.length === 0) {
      elements.overviewAccounts.innerHTML = `<div class="p-4 text-center font-code-sm text-code-sm text-outline">No accounts found in database.</div>`;
      return;
    }

    elements.overviewAccounts.innerHTML = accounts.slice(0, 5).map(acc => `
      <div class="p-space-sm rounded-DEFAULT bg-surface-container-low hover:bg-surface-container transition-colors border border-surface-container flex items-center justify-between">
        <div class="flex items-center gap-space-sm">
          <div class="w-8 h-8 rounded-DEFAULT bg-surface-container-high flex items-center justify-center font-code-sm text-code-sm text-secondary font-semibold">
            ${acc.account_id}
          </div>
          <div>
            <div class="font-headline-sm text-body-sm font-semibold text-on-surface leading-tight">${acc.owner_name || `Account ${acc.account_id}`}</div>
            <div class="font-code-sm text-code-sm text-outline">ACC-${acc.account_id} • User #${acc.user_id || 1}</div>
          </div>
        </div>
        <div class="text-right">
          <div class="font-code-md text-code-md font-semibold text-on-surface">${formatMoney(acc.balance)}</div>
          <div class="font-code-sm text-[11px] text-primary">Available 2PL</div>
        </div>
      </div>
    `).join('');

    // Also populate transfer modal select dropdowns
    if (elements.modalFromAccount && elements.modalToAccount) {
      const optionsHtml = accounts.map(a => `
        <option value="${a.account_id}">Account ${a.account_id} — ${a.owner_name || 'Account'} (${formatMoney(a.balance)})</option>
      `).join('');
      elements.modalFromAccount.innerHTML = optionsHtml;
      elements.modalToAccount.innerHTML = optionsHtml;
      if (accounts.length > 1) {
        elements.modalToAccount.selectedIndex = 1;
      }
    }
  }

  // Render Overview Recent Transactions
  function renderOverviewTxns(txns) {
    if (!elements.overviewTxns) return;
    const recent = txns.slice(0, 6);
    elements.overviewTxns.innerHTML = recent.map(tx => {
      const idStr = tx.id_str || `TXN-${tx.transaction_id || tx.id}`;
      const amount = tx.amount || 0;
      const worker = tx.worker_id || 'T-01';
      const lockMode = tx.lock_mode || 'EXCLUSIVE';
      const timeStr = tx.timestamp || (tx.created_at ? tx.created_at.split(' ')[1] : '10:42:18');

      return `
        <tr class="hover:bg-surface-container-low transition-colors group">
          <td class="py-2.5 px-3 font-code-sm text-code-sm font-medium text-primary">
            <span class="cursor-pointer hover:underline" onclick="PayLockApp.inspectTransaction('${tx.transaction_id || tx.id}')">${idStr}</span>
          </td>
          <td class="py-2.5 px-3 font-code-sm text-code-sm text-outline">${timeStr}</td>
          <td class="py-2.5 px-3 font-code-sm text-code-sm text-on-surface">ACC-${tx.from_account_id || tx.sender}</td>
          <td class="py-2.5 px-3 font-code-sm text-code-sm text-on-surface">ACC-${tx.to_account_id || tx.receiver}</td>
          <td class="py-2.5 px-3 font-code-md text-code-md font-semibold text-on-surface">${formatMoney(amount)}</td>
          <td class="py-2.5 px-3 font-code-sm text-code-sm text-outline">${lockMode}</td>
          <td class="py-2.5 px-3 font-code-sm text-code-sm text-secondary">${worker}</td>
          <td class="py-2.5 px-3">${getStatusBadge(tx.status)}</td>
          <td class="py-2.5 px-3 text-right">
            <button onclick="PayLockApp.inspectTransaction('${tx.transaction_id || tx.id}')" class="p-1 rounded-DEFAULT hover:bg-surface-container text-outline hover:text-on-surface transition-colors" title="Inspect TXN">
              <span class="material-symbols-outlined text-[18px]">troubleshoot</span>
            </button>
          </td>
        </tr>
      `;
    }).join('');
  }

  // Render Full Transactions Table
  function renderTransactionsTable(txns) {
    state.transactions = txns;
    if (!elements.txnsTableBody) return;

    // Filter by active pill
    let filtered = txns;
    if (state.activeFilter && state.activeFilter !== 'All') {
      filtered = filtered.filter(t => {
        const s = (t.status || '').toUpperCase();
        if (state.activeFilter === 'COMMITTED') return s === 'COMMITTED' || s === 'SUCCESS';
        if (state.activeFilter === 'PROCESSING') return s === 'PROCESSING' || s === 'PENDING';
        if (state.activeFilter === 'WAITING') return s === 'WAITING' || s === 'QUEUED';
        if (state.activeFilter === 'ROLLED_BACK') return s === 'ROLLED_BACK' || s === 'ABORTED';
        if (state.activeFilter === 'DEADLOCK') return s === 'DEADLOCK' || s === 'FAILED';
        return true;
      });
    }

    // Filter by query
    if (state.searchQuery) {
      const q = state.searchQuery.toLowerCase();
      filtered = filtered.filter(t => {
        const idStr = String(t.id_str || t.transaction_id || t.id).toLowerCase();
        const fromStr = String(t.from_account_id || t.sender).toLowerCase();
        const toStr = String(t.to_account_id || t.receiver).toLowerCase();
        return idStr.includes(q) || fromStr.includes(q) || toStr.includes(q);
      });
    }

    // Update Counts in footer
    if (elements.tableShowingRange) {
      elements.tableShowingRange.textContent = `1-${filtered.length}`;
    }
    if (elements.tableTotalCount) {
      elements.tableTotalCount.textContent = txns.length;
    }

    if (filtered.length === 0) {
      elements.txnsTableBody.innerHTML = `
        <tr>
          <td colspan="10" class="py-8 text-center font-code-sm text-code-sm text-outline">
            No transactions match the selected filter criteria.
          </td>
        </tr>
      `;
      return;
    }

    elements.txnsTableBody.innerHTML = filtered.map(tx => {
      const idStr = tx.id_str || `TXN-${tx.transaction_id || tx.id}`;
      const amount = tx.amount || 0;
      const worker = tx.worker_id || 'T-01';
      const lockMode = tx.lock_mode || 'EXCLUSIVE';
      const latency = (tx.latency_ms || 18) + 'ms';
      const timeStr = tx.timestamp || (tx.created_at ? tx.created_at : '10:42:18');

      return `
        <tr class="hover:bg-surface-container-low transition-colors group">
          <td class="py-2.5 px-4 font-code-sm text-code-sm font-semibold text-primary">
            <span class="cursor-pointer hover:underline" onclick="PayLockApp.inspectTransaction('${tx.transaction_id || tx.id}')">${idStr}</span>
          </td>
          <td class="py-2.5 px-4 font-code-sm text-code-sm text-outline">${timeStr}</td>
          <td class="py-2.5 px-4 font-code-sm text-code-sm text-on-surface">ACC-${tx.from_account_id || tx.sender}</td>
          <td class="py-2.5 px-4 font-code-sm text-code-sm text-on-surface">ACC-${tx.to_account_id || tx.receiver}</td>
          <td class="py-2.5 px-4 font-code-md text-code-md font-semibold text-on-surface">${formatMoney(amount)}</td>
          <td class="py-2.5 px-4 font-code-sm text-code-sm text-outline">
            <span class="px-1.5 py-0.5 rounded bg-surface-container border border-surface-container-high">${lockMode}</span>
          </td>
          <td class="py-2.5 px-4 font-code-sm text-code-sm text-secondary">${worker}</td>
          <td class="py-2.5 px-4 font-code-sm text-code-sm text-outline">${latency}</td>
          <td class="py-2.5 px-4">${getStatusBadge(tx.status)}</td>
          <td class="py-2.5 px-4 text-right">
            <button onclick="PayLockApp.inspectTransaction('${tx.transaction_id || tx.id}')" class="px-2 py-1 rounded-DEFAULT bg-surface-container hover:bg-surface-container-high text-on-surface border border-surface-container-high font-code-sm text-code-sm transition-colors">
              Inspect
            </button>
          </td>
        </tr>
      `;
    }).join('');
  }

  // Render Concurrency Thread Pool Pipeline Board (Vector Flow Board from Stitch)
  function renderThreadPoolPipeline() {
    if (!elements.threadPipelineBoard) return;

    const workers = [
      { id: "T-01", name: "Worker T-01", state: "RUNNING", txn: "TXN-10485", stage: "DB Balance Write", color: "primary" },
      { id: "T-02", name: "Worker T-02", state: "RUNNING", txn: "TXN-10483", stage: "ACID Validation", color: "primary" },
      { id: "T-03", name: "Worker T-03", state: "IDLE", txn: "—", stage: "Awaiting Dispatch", color: "outline" },
      { id: "T-04", name: "Worker T-04", state: "MUTEX_WAIT", txn: "TXN-10482", stage: "Wait Lock ACC-101", color: "tertiary" },
      { id: "T-05", name: "Worker T-05", state: "COMMITTED", txn: "TXN-10484", stage: "Post-Commit Flush", color: "secondary" },
      { id: "T-06", name: "Worker T-06", state: "CYCLE_DETECTED", txn: "TXN-10480", stage: "Deadlock Abort/Rollback", color: "error" }
    ];

    elements.threadPipelineBoard.innerHTML = workers.map(w => {
      const isRunning = w.state === 'RUNNING';
      const isWait = w.state === 'MUTEX_WAIT';
      const isDeadlock = w.state === 'CYCLE_DETECTED';

      const stateDotClass = isRunning ? 'bg-primary animate-ping' : isWait ? 'bg-tertiary' : isDeadlock ? 'bg-error animate-ping' : 'bg-surface-container-highest';
      const stateTextColor = isRunning ? 'text-primary' : isWait ? 'text-tertiary' : isDeadlock ? 'text-error' : 'text-outline';

      return `
        <div class="flex items-center min-w-[750px] gap-2 p-2 rounded-DEFAULT bg-surface-container/60 hover:bg-surface-container transition-colors">
          <div class="w-32 flex-shrink-0 flex items-center justify-between px-2.5 py-1 bg-surface-container-high rounded-DEFAULT">
            <span class="font-code-sm text-code-sm font-semibold ${stateTextColor}">${w.name}</span>
            <span class="w-2 h-2 rounded-full ${stateDotClass}"></span>
          </div>
          <div class="flex items-center text-primary flex-shrink-0 font-code-sm text-code-sm">─────►</div>
          <div class="px-3 py-1 rounded-DEFAULT bg-surface-container-highest text-on-surface flex items-center gap-2">
            <span class="material-symbols-outlined text-[16px] text-primary">receipt</span>
            <span class="font-code-sm text-code-sm font-medium">${w.txn}</span>
          </div>
          <div class="flex items-center text-primary flex-shrink-0 font-code-sm text-code-sm">─────►</div>
          <div class="px-3 py-1 rounded-DEFAULT bg-surface-container-highest text-on-surface flex items-center gap-2">
            <span class="material-symbols-outlined text-[16px] ${w.color === 'error' ? 'text-error' : 'text-secondary'}">account_tree</span>
            <span class="font-code-sm text-code-sm">${w.stage}</span>
          </div>
          <div class="flex items-center text-primary flex-shrink-0 font-code-sm text-code-sm">─────►</div>
          <div class="px-2.5 py-1 rounded-DEFAULT bg-surface-container font-code-sm text-code-sm ${stateTextColor} font-semibold">
            ${w.state}
          </div>
        </div>
      `;
    }).join('');
  }

  // Render Concurrency Locks Table
  function renderLocks(locks) {
    state.locks = locks;
    if (!elements.locksTableBody) return;

    if (!locks || locks.length === 0) {
      elements.locksTableBody.innerHTML = `<tr><td colspan="5" class="p-4 text-center font-code-sm text-code-sm text-outline">No active locks held.</td></tr>`;
      if (elements.activeLocksCount) elements.activeLocksCount.textContent = '0 Locks Active';
      return;
    }

    if (elements.activeLocksCount) {
      elements.activeLocksCount.textContent = `${locks.length} Locks Tracked`;
    }

    elements.locksTableBody.innerHTML = locks.map(l => {
      const waiters = Array.isArray(l.waiting_txns) && l.waiting_txns.length > 0
        ? l.waiting_txns.map(w => `TXN-${w}`).join(', ')
        : 'None';

      const modeBadge = l.mode === 'EXCLUSIVE'
        ? `<span class="px-1.5 py-0.5 rounded bg-primary/10 text-primary font-code-sm text-code-sm">EXCLUSIVE</span>`
        : `<span class="px-1.5 py-0.5 rounded bg-secondary/10 text-secondary font-code-sm text-code-sm">SHARED</span>`;

      return `
        <tr class="hover:bg-surface-container-low transition-colors">
          <td class="py-2.5 px-3 font-code-sm text-code-sm font-medium text-on-surface">${l.resource}</td>
          <td class="py-2.5 px-3">${modeBadge}</td>
          <td class="py-2.5 px-3 font-code-sm text-code-sm text-primary">TXN-${l.holder_txn}</td>
          <td class="py-2.5 px-3 font-code-sm text-code-sm ${waiters !== 'None' ? 'text-error font-semibold' : 'text-outline'}">${waiters}</td>
          <td class="py-2.5 px-3 font-code-sm text-code-sm text-outline">${l.acquired_at || '10:42:18.420'}</td>
        </tr>
      `;
    }).join('');
  }

  // Render Deadlock Recovery History
  function renderRecoveryHistory(recoveries) {
    state.recoveries = recoveries;
    if (!elements.recoveryTableBody) return;

    if (!recoveries || recoveries.length === 0) {
      elements.recoveryTableBody.innerHTML = `<tr><td colspan="7" class="p-4 text-center font-code-sm text-code-sm text-outline">No deadlock events recorded.</td></tr>`;
      return;
    }

    elements.recoveryTableBody.innerHTML = recoveries.map(r => `
      <tr class="hover:bg-surface-container-low transition-colors">
        <td class="py-2.5 px-3 font-code-sm text-code-sm font-semibold text-on-surface">${r.id || 'REC-892'}</td>
        <td class="py-2.5 px-3 font-code-sm text-code-sm text-outline">${r.timestamp || '10:41:45.920'}</td>
        <td class="py-2.5 px-3 font-code-sm text-code-sm text-error font-bold">TXN-${r.victim_txn}</td>
        <td class="py-2.5 px-3 font-code-sm text-code-sm text-outline">${r.cycle || '10480 -> 10482 -> 10485 -> 10480'}</td>
        <td class="py-2.5 px-3 font-code-sm text-code-sm text-on-surface">${r.action || 'ABORT_ROLLBACK'}</td>
        <td class="py-2.5 px-3 font-code-sm text-code-sm text-secondary">${Array.isArray(r.locks_released) ? r.locks_released.join(', ') : 'ACC-103 Mutex'}</td>
        <td class="py-2.5 px-3"><span class="px-1.5 py-0.5 rounded bg-primary/10 text-primary font-code-sm text-code-sm">COMPLETED</span></td>
      </tr>
    `).join('');
  }

  // Render 64 Frames Grid (OS Paging simulation)
  function renderFramesGrid() {
    if (!elements.framesGrid) return;
    let html = '';
    for (let i = 0; i < 64; i++) {
      let bg = 'bg-surface-container-highest';
      if (i < 42) bg = 'bg-primary';
      else if (i === 42 || i === 43) bg = 'bg-error';
      html += `<div class="w-full aspect-square rounded-DEFAULT ${bg} flex items-center justify-center font-code-sm text-[9px] text-surface font-semibold" title="Frame #${i}">${i}</div>`;
    }
    elements.framesGrid.innerHTML = html;
  }

  // Transaction Inspector Action
  function inspectTransaction(id) {
    const txn = state.transactions.find(t => String(t.transaction_id || t.id) === String(id)) || window.MockData?.detailSample;
    if (!txn) {
      showToast('Error', 'Transaction not found', 'error');
      return;
    }

    state.selectedTxn = txn;
    const idStr = txn.id_str || `TXN-${txn.transaction_id || txn.id}`;

    if (elements.detailBreadcrumbId) elements.detailBreadcrumbId.textContent = idStr;
    if (elements.detailTitle) elements.detailTitle.textContent = `Inspector: ${idStr}`;
    if (elements.detailStatusBadge) {
      elements.detailStatusBadge.textContent = txn.status || 'COMMITTED';
      elements.detailStatusBadge.className = txn.status === 'COMMITTED'
        ? 'px-2 py-0.5 rounded-DEFAULT bg-primary/10 text-primary font-code-sm text-code-sm border border-primary/20'
        : 'px-2 py-0.5 rounded-DEFAULT bg-error-container/40 text-error font-code-sm text-code-sm border border-error/30';
    }
    if (elements.detailHashString) {
      elements.detailHashString.textContent = `Hash: 0x${Math.abs(Number(txn.transaction_id || txn.id || 10485)).toString(16).padStart(16, '0')} • Thread: ${txn.worker_id || 'Worker T-01'}`;
    }
    if (elements.detailTotalLatency) {
      elements.detailTotalLatency.textContent = `${txn.latency_ms || 124} ms TOTAL`;
    }

    // Populate Balance Diffs
    if (elements.detailBalanceDiffs) {
      const fromAcc = txn.from_account_id || txn.sender || 101;
      const toAcc = txn.to_account_id || txn.receiver || 102;
      const amt = txn.amount || 1250;
      elements.detailBalanceDiffs.innerHTML = `
        <div class="p-space-sm rounded-DEFAULT bg-surface-container-low border border-surface-container flex items-center justify-between">
          <div>
            <div class="text-on-surface font-medium">Source: Account ${fromAcc}</div>
            <div class="text-outline text-[11px]">Strict 2PL Exclusive Lock Acquired</div>
          </div>
          <span class="text-error font-bold text-headline-sm">-${formatMoney(amt)}</span>
        </div>
        <div class="p-space-sm rounded-DEFAULT bg-surface-container-low border border-surface-container flex items-center justify-between">
          <div>
            <div class="text-on-surface font-medium">Destination: Account ${toAcc}</div>
            <div class="text-outline text-[11px]">Strict 2PL Exclusive Lock Acquired</div>
          </div>
          <span class="text-primary font-bold text-headline-sm">+${formatMoney(amt)}</span>
        </div>
      `;
    }

    switchView('transaction-detail');
  }

  // Update System KPIs
  function updateKPIs(stats) {
    if (!stats) return;
    const tx = stats.transactions || {};
    const conc = stats.concurrency || {};
    const lk = stats.locks || {};
    const dl = stats.deadlocks || {};
    const pg = stats.paging_simulation || stats.paging || {};

    if (elements.kpiTotalVal) elements.kpiTotalVal.textContent = (tx.total || 12482).toLocaleString();
    if (elements.kpiCommittedVal) elements.kpiCommittedVal.textContent = (tx.committed || 12210).toLocaleString();
    if (elements.kpiRollbackVal) elements.kpiRollbackVal.textContent = (tx.rolled_back || 272).toLocaleString();
    if (elements.kpiLocksVal) elements.kpiLocksVal.textContent = (lk.active_holders || 7);
    if (elements.kpiDeadlocksVal) elements.kpiDeadlocksVal.textContent = (dl.total_recoveries !== undefined ? dl.total_recoveries : 2);
    if (elements.kpiThreadsVal) elements.kpiThreadsVal.textContent = (conc.active_threads || 18);

    if (elements.sideWorkersCount) elements.sideWorkersCount.textContent = `${conc.active_threads || 18} Active`;
    if (elements.sideLocksCount) elements.sideLocksCount.textContent = `${lk.active_holders || 7} Mutex`;
    if (elements.deadlockBadge) elements.deadlockBadge.textContent = dl.total_recoveries !== undefined ? dl.total_recoveries : 0;
  }

  // Refresh All Data from C++ API
  async function refreshData() {
    try {
      const [health, stats, accounts, txns, locks, wfg, recoveries] = await Promise.all([
        window.api.getHealth(),
        window.api.getSystemStats(),
        window.api.getAccounts(20),
        window.api.getTransactions(25),
        window.api.getLocks(),
        window.api.getWaitForGraph(),
        window.api.getRecoveryHistory()
      ]);

      // Update Header & Backend Status indicator
      const isOnline = health && health.status === 'healthy';
      if (elements.backendStatusDot) {
        elements.backendStatusDot.className = isOnline
          ? 'w-1.5 h-1.5 rounded-full bg-primary animate-pulse'
          : 'w-1.5 h-1.5 rounded-full bg-secondary';
      }
      if (elements.backendVersionChip) {
        elements.backendVersionChip.textContent = isOnline ? 'C++ LIVE' : 'C++ MOCK';
      }
      if (elements.clusterText) {
        elements.clusterText.textContent = isOnline
          ? 'CLUSTER: STABLE • 4.2k TPS'
          : 'STANDALONE PROTOCOL • DEMO';
      }

      // Render Components
      if (stats) updateKPIs(stats);
      if (accounts) renderAccounts(accounts);
      if (txns) {
        renderOverviewTxns(txns);
        renderTransactionsTable(txns);
      }
      if (locks) renderLocks(locks);
      if (recoveries) renderRecoveryHistory(recoveries);
    } catch (err) {
      console.warn('PayLock sync notice:', err);
    }
  }

  // Transfer Form Submission
  async function handleTransferSubmit(e) {
    e.preventDefault();
    const fromId = parseInt(elements.modalFromAccount.value, 10);
    const toId = parseInt(elements.modalToAccount.value, 10);
    const amount = parseFloat(elements.modalAmount.value);

    // Client Validation
    if (fromId === toId) {
      showModalAlert('Source and destination accounts must be distinct.', 'error');
      return;
    }
    if (isNaN(amount) || amount <= 0) {
      showModalAlert('Transfer amount must be greater than zero.', 'error');
      return;
    }

    // Check balance
    const sourceAcc = state.accounts.find(a => a.account_id === fromId);
    if (sourceAcc && sourceAcc.balance < amount) {
      showModalAlert(`Insufficient funds in Account ${fromId}. Balance: ${formatMoney(sourceAcc.balance)}`, 'error');
      return;
    }

    // Pending State
    elements.modalSubmitBtn.disabled = true;
    elements.modalSubmitText.textContent = 'Processing 2PL...';
    hideModalAlert();

    try {
      const response = await window.api.transfer(fromId, toId, amount);
      const data = response.data || {};
      const newTxnId = data.transaction_id || `TXN-${Math.floor(Math.random() * 1000 + 10500)}`;

      showToast('Transfer Committed', `Successfully transferred ${formatMoney(amount)} from ACC-${fromId} to ACC-${toId} (ID: ${newTxnId})`, 'success');
      closeTransferModal();

      // Refresh data
      await refreshData();
    } catch (err) {
      const msg = err.message || 'Transaction aborted by backend engine';
      showModalAlert(`Transfer Failed: ${msg}`, 'error');
      showToast('Transaction Rolled Back', msg, 'error');
    } finally {
      elements.modalSubmitBtn.disabled = false;
      elements.modalSubmitText.textContent = 'Execute Transfer';
    }
  }

  function showModalAlert(msg, type = 'error') {
    if (!elements.modalAlert) return;
    elements.modalAlert.textContent = msg;
    elements.modalAlert.className = type === 'error'
      ? 'p-space-sm rounded-DEFAULT font-code-sm text-code-sm flex items-center gap-2 bg-error-container/30 text-error border border-error/40'
      : 'p-space-sm rounded-DEFAULT font-code-sm text-code-sm flex items-center gap-2 bg-primary/10 text-primary border border-primary/30';
    elements.modalAlert.classList.remove('hidden');
  }

  function hideModalAlert() {
    if (elements.modalAlert) {
      elements.modalAlert.classList.add('hidden');
      elements.modalAlert.textContent = '';
    }
  }

  function openTransferModal() {
    if (elements.transferModal) {
      hideModalAlert();
      elements.transferModal.classList.add('open');
      if (elements.modalAmount) elements.modalAmount.focus();
    }
  }

  function closeTransferModal() {
    if (elements.transferModal) {
      elements.transferModal.classList.remove('open');
      hideModalAlert();
    }
  }

  // Simulate Traffic Trigger
  async function triggerSimulation() {
    showToast('Simulation Triggered', 'Dispatching 4 concurrent worker transfers to test 2PL concurrency', 'info');
    if (state.accounts.length >= 2) {
      const acc1 = state.accounts[0].account_id;
      const acc2 = state.accounts[1].account_id;
      try {
        await window.api.transfer(acc1, acc2, 10.00);
        await refreshData();
        showToast('Worker Committed', `Concurrent transfer completed on ACC-${acc1} -> ACC-${acc2}`, 'success');
      } catch (err) {
        showToast('Worker Notification', err.message, 'info');
      }
    }
  }

  // Setup Event Handlers
  function bindEvents() {
    // Nav Items click
    elements.navItems.forEach(item => {
      item.addEventListener('click', (e) => {
        e.preventDefault();
        const view = item.getAttribute('data-view') || item.getAttribute('href')?.replace('#', '');
        switchView(view);
      });
    });

    // Brand click -> Overview
    document.getElementById('nav-brand-btn')?.addEventListener('click', () => switchView('overview'));
    document.getElementById('view-all-accounts-btn')?.addEventListener('click', () => switchView('overview'));
    document.getElementById('view-all-txns-btn')?.addEventListener('click', () => switchView('transactions'));
    document.getElementById('transactions-new-btn')?.addEventListener('click', openTransferModal);
    elements.detailBackBtn?.addEventListener('click', () => switchView('transactions'));

    // Sync button
    elements.syncBtn?.addEventListener('click', async () => {
      showToast('Synchronizing', 'Pulling latest lock table and account state from C++ API', 'info');
      await refreshData();
    });

    // Transfer Modal
    elements.openModalBtn?.addEventListener('click', openTransferModal);
    elements.closeModalBtn?.addEventListener('click', closeTransferModal);
    elements.cancelModalBtn?.addEventListener('click', closeTransferModal);
    elements.transferModal?.addEventListener('click', (e) => {
      if (e.target === elements.transferModal) closeTransferModal();
    });
    elements.transferForm?.addEventListener('submit', handleTransferSubmit);

    // Simulation Button
    elements.simulateBtn?.addEventListener('click', triggerSimulation);

    // Status Pills Filtering on Transactions Monitor
    elements.statusPills.forEach(pill => {
      pill.addEventListener('click', () => {
        elements.statusPills.forEach(p => {
          p.className = 'status-pill flex items-center gap-1.5 px-space-sm py-1 rounded-DEFAULT bg-surface-container hover:bg-surface-container-high text-on-surface-variant hover:text-on-surface font-headline-sm text-label-md transition-colors';
        });
        pill.className = 'status-pill flex items-center gap-1.5 px-space-sm py-1 rounded-DEFAULT bg-primary text-on-primary font-headline-sm text-label-md font-medium';
        state.activeFilter = pill.getAttribute('data-status') || 'All';
        renderTransactionsTable(state.transactions);
      });
    });

    // Transactions Search Filter
    elements.txnsSearch?.addEventListener('input', (e) => {
      state.searchQuery = e.target.value;
      renderTransactionsTable(state.transactions);
    });

    // Global Search Focus Shortcut
    elements.globalSearch?.addEventListener('keydown', (e) => {
      if (e.key === 'Enter') {
        state.searchQuery = e.target.value;
        switchView('transactions');
        renderTransactionsTable(state.transactions);
      }
    });

    // Keyboard Shortcuts (Stitch Specification: ⌘1-6, N, ⌘K, Escape)
    window.addEventListener('keydown', (e) => {
      const isCmdOrCtrl = e.metaKey || e.ctrlKey;
      if (isCmdOrCtrl && e.key === '1') { e.preventDefault(); switchView('overview'); }
      if (isCmdOrCtrl && e.key === '2') { e.preventDefault(); switchView('transactions'); }
      if (isCmdOrCtrl && e.key === '3') { e.preventDefault(); switchView('transaction-detail'); }
      if (isCmdOrCtrl && e.key === '4') { e.preventDefault(); switchView('accounts'); }
      if (isCmdOrCtrl && e.key === '5') { e.preventDefault(); switchView('concurrency'); }
      if (isCmdOrCtrl && e.key === '6') { e.preventDefault(); switchView('lock-manager'); }
      if (isCmdOrCtrl && e.key.toLowerCase() === 'k') {
        e.preventDefault();
        elements.globalSearch?.focus();
      }
      if (e.key.toLowerCase() === 'n' && !['INPUT', 'SELECT', 'TEXTAREA'].includes(document.activeElement.tagName)) {
        e.preventDefault();
        openTransferModal();
      }
      if (e.key === 'Escape') {
        closeTransferModal();
      }
    });
  }

  // App Initialization
  async function init() {
    initElements();
    bindEvents();
    renderThreadPoolPipeline();
    renderFramesGrid();

    // Start Telemetry Clock
    setInterval(updateUptimeClock, 1000);
    updateUptimeClock();

    // Initial Data Fetch
    await refreshData();

    // Periodic Background Refresh (every 6 seconds)
    setInterval(refreshData, 6000);
  }

  // Export public API
  window.PayLockApp = {
    init,
    switchView,
    inspectTransaction,
    openTransferModal,
    closeTransferModal,
    refreshData
  };

  // Launch on DOM ready
  if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', init);
  } else {
    init();
  }
})();
