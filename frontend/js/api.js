/**
 * PayLock — Native C++ REST API Client
 * Targets: http://127.0.0.1:8080/api/v1
 * Features: Type-safe methods, timeout handling, error envelopes, and seamless mock fallback.
 */

const defaultHost = (typeof window !== 'undefined' && window.location.hostname && window.location.hostname !== '') 
  ? window.location.hostname 
  : '127.0.0.1';

class PayLockApiClient {
  constructor(baseUrl = `http://${defaultHost}:8080/api/v1`) {
    this.baseUrl = baseUrl;
    this.isOnline = false;
    this.lastLatency = null;
    this.onStatusChangeCallbacks = [];
  }

  setBaseUrl(url) {
    this.baseUrl = url.replace(/\/+$/, '');
  }

  onStatusChange(callback) {
    this.onStatusChangeCallbacks.push(callback);
  }

  _notifyStatus(isOnline) {
    if (this.isOnline !== isOnline) {
      this.isOnline = isOnline;
      this.onStatusChangeCallbacks.forEach(cb => cb(isOnline));
    }
  }

  async _request(endpoint, options = {}) {
    const url = `${this.baseUrl}${endpoint}`;
    const timeoutMs = options.timeout || 4000;
    const controller = new AbortController();
    const timer = setTimeout(() => controller.abort(), timeoutMs);

    const startTime = performance.now();
    try {
      const headers = {
        'Accept': 'application/json',
        ...(options.headers || {})
      };
      if (options.body || options.method === 'POST' || options.method === 'PUT') {
        headers['Content-Type'] = 'application/json';
      }

      const response = await fetch(url, {
        ...options,
        signal: controller.signal,
        headers
      });
      clearTimeout(timer);
      this.lastLatency = Math.round(performance.now() - startTime);

      const json = await response.json().catch(() => null);

      if (!response.ok) {
        this._notifyStatus(true);
        const errMessage = json?.error?.message || json?.message || `HTTP ${response.status}: ${response.statusText}`;
        const err = new Error(errMessage);
        err.status = response.status;
        err.code = json?.error?.code || 'HTTP_ERROR';
        err.data = json;
        throw err;
      }

      this._notifyStatus(true);
      return json;
    } catch (err) {
      clearTimeout(timer);
      if (err.name === 'AbortError') {
        err.message = `Request timeout after ${timeoutMs}ms`;
      }
      if (err.message.includes('Failed to fetch') || err.name === 'AbortError') {
        this._notifyStatus(false);
      }
      throw err;
    }
  }

  // 1. System & Health
  async getHealth() {
    try {
      const res = await this._request('/health', { timeout: 2500 });
      return res.data;
    } catch (err) {
      return {
        status: "offline",
        server: "unreachable",
        version: "C++ v2.4",
        database: { connected: false, error: err.message }
      };
    }
  }

  async getSystemStats() {
    try {
      const res = await this._request('/system/stats');
      return res.data;
    } catch (err) {
      // Fallback to MockData
      return window.MockData ? window.MockData.systemStats : null;
    }
  }

  // 2. Accounts
  async getAccounts(limit = 20, offset = 0) {
    try {
      const res = await this._request(`/accounts?limit=${limit}&offset=${offset}`);
      return res.data.accounts || [];
    } catch (err) {
      return window.MockData ? window.MockData.accounts : [];
    }
  }

  async getAccount(id) {
    const res = await this._request(`/accounts/${id}`);
    return res.data;
  }

  // 3. Transactions
  async getTransactions(limit = 50, offset = 0, status = '') {
    let liveTxns = [];
    try {
      let query = `/transactions?limit=${limit}&offset=${offset}`;
      const res = await this._request(query);
      const list = res?.data?.transactions || [];
      liveTxns = list.map(t => ({
        ...t,
        id_str: t.id_str || `TXN-${t.transaction_id || t.id}`,
        from_account_id: t.from_account_id ?? t.sender_account ?? t.sender ?? 0,
        to_account_id: t.to_account_id ?? t.receiver_account ?? t.receiver ?? 0,
        sender_account: t.sender_account ?? t.from_account_id ?? t.sender ?? 0,
        receiver_account: t.receiver_account ?? t.to_account_id ?? t.receiver ?? 0,
        timestamp: t.timestamp || (t.created_at ? (t.created_at.includes(' ') ? t.created_at.split(' ')[1] : t.created_at) : '10:42:18'),
        lock_mode: t.lock_mode || 'EXCLUSIVE',
        worker_id: t.worker_id || 'T-01',
        latency_ms: t.latency_ms || 14
      }));
    } catch (err) {
      // Offline fallback handled below
    }

    // Active state transactions (PROCESSING, WAITING, DEADLOCK, ROLLED_BACK)
    // Ensures the monitor displays all OS concurrency states alongside live DB records
    const activeStates = (window.MockData ? window.MockData.transactions : []).map(t => ({
      ...t,
      id_str: t.id_str || `TXN-${t.transaction_id || t.id}`,
      from_account_id: t.from_account_id ?? t.sender_account ?? t.sender ?? 0,
      to_account_id: t.to_account_id ?? t.receiver_account ?? t.receiver ?? 0,
      sender_account: t.sender_account ?? t.from_account_id ?? t.sender ?? 0,
      receiver_account: t.receiver_account ?? t.to_account_id ?? t.receiver ?? 0,
      timestamp: t.timestamp || (t.created_at ? (t.created_at.includes(' ') ? t.created_at.split(' ')[1] : t.created_at) : '10:42:18'),
      lock_mode: t.lock_mode || 'EXCLUSIVE',
      worker_id: t.worker_id || 'T-01',
      latency_ms: t.latency_ms || 18
    }));

    // Deduplicate by transaction_id: Live DB records take precedence
    const liveIds = new Set(liveTxns.map(t => t.transaction_id));
    const merged = [
      ...liveTxns,
      ...activeStates.filter(t => !liveIds.has(t.transaction_id))
    ];

    return merged;
  }

  async getTransaction(id) {
    try {
      const res = await this._request(`/transactions/${id}`);
      return res.data;
    } catch (err) {
      if (window.MockData && window.MockData.detailSample) {
        return window.MockData.detailSample;
      }
      throw err;
    }
  }

  async transfer(fromAccountId, toAccountId, amount) {
    const from = parseInt(fromAccountId, 10);
    const to = parseInt(toAccountId, 10);
    const amt = parseFloat(amount);
    const body = {
      from_account: from,
      to_account: to,
      sender_account: from,
      receiver_account: to,
      from_account_id: from,
      to_account_id: to,
      amount: amt
    };
    try {
      return await this._request('/transactions/transfer', {
        method: 'POST',
        body: JSON.stringify(body)
      });
    } catch (err) {
      const isNetwork = !this.isOnline || (err.message && (err.message.includes('fetch') || err.message.includes('timeout') || err.message.includes('NetworkError')));
      if (isNetwork) {
        const fallbackTxnId = Math.floor(Math.random() * 1000 + 10500);
        const fallbackTx = {
          transaction_id: fallbackTxnId,
          id_str: `TXN-${fallbackTxnId}`,
          from_account_id: from,
          to_account_id: to,
          sender_account: from,
          receiver_account: to,
          amount: amt,
          status: "COMMITTED",
          lock_mode: "EXCLUSIVE",
          worker_id: "T-01",
          latency_ms: 12.8,
          timestamp: new Date().toTimeString().split(' ')[0],
          created_at: new Date().toISOString().replace('T', ' ').substring(0, 19)
        };
        if (window.MockData && Array.isArray(window.MockData.transactions)) {
          window.MockData.transactions.unshift(fallbackTx);
        }
        return {
          success: true,
          message: "Transfer completed and committed successfully (Offline Demo)",
          data: fallbackTx
        };
      }
      throw err;
    }
  }

  async deposit(accountId, amount) {
    return await this._request('/transactions/deposit', {
      method: 'POST',
      body: JSON.stringify({
        account_id: parseInt(accountId, 10),
        amount: parseFloat(amount)
      })
    });
  }

  async withdraw(accountId, amount) {
    return await this._request('/transactions/withdraw', {
      method: 'POST',
      body: JSON.stringify({
        account_id: parseInt(accountId, 10),
        amount: parseFloat(amount)
      })
    });
  }

  // 4. Concurrency & Locks
  async getConcurrencyStatus() {
    try {
      const res = await this._request('/concurrency/status');
      return res.data;
    } catch (err) {
      if (window.MockData && window.MockData.concurrency) {
        return { ...window.MockData.concurrency, is_mock: true, error: err.message };
      }
      return null;
    }
  }

  async getConcurrencyTransactions() {
    try {
      const res = await this._request('/concurrency/transactions');
      return res.data;
    } catch (err) {
      return null;
    }
  }

  async getLocks() {
    try {
      const res = await this._request('/locks');
      if (res && res.data) {
        const raw = Array.isArray(res.data.locks)
          ? res.data.locks
          : (Array.isArray(res.data) ? res.data : []);
        
        return raw.map(l => ({
          resource: l.resource_name || l.resource || (l.account_id ? `ACC-${l.account_id}` : 'RESOURCE'),
          mode: l.current_mode || l.mode || 'EXCLUSIVE',
          holder_txn: Array.isArray(l.holders) && l.holders.length > 0
            ? l.holders.join(', ')
            : (l.holder_txn || 'None'),
          waiting_txns: Array.isArray(l.wait_queue)
            ? l.wait_queue.map(w => w.transaction_id || w)
            : (l.waiting_txns || []),
          acquired_at: l.acquired_at || new Date().toTimeString().split(' ')[0]
        }));
      }
      return window.MockData ? window.MockData.concurrency.locks : [];
    } catch (err) {
      return window.MockData ? window.MockData.concurrency.locks : [];
    }
  }

  async getWaitForGraph() {
    try {
      const res = await this._request('/locks/wait-for-graph');
      return res.data;
    } catch (err) {
      if (window.MockData && window.MockData.concurrency && window.MockData.concurrency.wfg) {
        return { ...window.MockData.concurrency.wfg, is_mock: true, error: err.message };
      }
      return null;
    }
  }

  async getDeadlocks() {
    try {
      const res = await this._request('/deadlocks');
      return res.data;
    } catch (err) {
      return { deadlock_detected: false, cycles_found: 0, cycles: [] };
    }
  }

  async getRecoveryHistory() {
    try {
      const res = await this._request('/recovery/history');
      return res.data.recoveries || [];
    } catch (err) {
      return window.MockData ? window.MockData.concurrency.recovery_history : [];
    }
  }

  // 5. OS Resources & Paging
  async getResources() {
    try {
      const res = await this._request('/resources');
      return res.data;
    } catch (err) {
      return null;
    }
  }

  async getPagingStats() {
    try {
      const res = await this._request('/paging/stats');
      return res.data;
    } catch (err) {
      return null;
    }
  }

  async getPagingFrames() {
    try {
      const res = await this._request('/paging/frames');
      return res.data;
    } catch (err) {
      return null;
    }
  }
}

// Global singleton instance
window.api = new PayLockApiClient();
