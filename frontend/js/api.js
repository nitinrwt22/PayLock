/**
 * PayLock — Native C++ REST API Client
 * Targets: http://127.0.0.1:8080/api/v1
 * Features: Type-safe methods, timeout handling, error envelopes, and seamless mock fallback.
 */

class PayLockApiClient {
  constructor(baseUrl = 'http://127.0.0.1:8080/api/v1') {
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
      const response = await fetch(url, {
        ...options,
        signal: controller.signal,
        headers: {
          'Content-Type': 'application/json',
          'Accept': 'application/json',
          ...(options.headers || {})
        }
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
  async getTransactions(limit = 20, offset = 0, status = '') {
    try {
      let query = `/transactions?limit=${limit}&offset=${offset}`;
      if (status && status !== 'All') {
        query += `&status=${encodeURIComponent(status)}`;
      }
      const res = await this._request(query);
      return res.data.transactions || [];
    } catch (err) {
      return window.MockData ? window.MockData.transactions : [];
    }
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
    const body = {
      from_account_id: parseInt(fromAccountId, 10),
      to_account_id: parseInt(toAccountId, 10),
      amount: parseFloat(amount)
    };
    return await this._request('/transactions/transfer', {
      method: 'POST',
      body: JSON.stringify(body)
    });
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
      return window.MockData ? window.MockData.concurrency : null;
    }
  }

  async getLocks() {
    try {
      const res = await this._request('/locks');
      return res.data;
    } catch (err) {
      return window.MockData ? window.MockData.concurrency.locks : [];
    }
  }

  async getWaitForGraph() {
    try {
      const res = await this._request('/locks/wait-for-graph');
      return res.data;
    } catch (err) {
      return window.MockData ? window.MockData.concurrency.wfg : null;
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
}

// Global singleton instance
window.api = new PayLockApiClient();
