document.addEventListener('DOMContentLoaded', () => {
    // State model mimicking backend
    const accounts = [
        { id: 101, name: "Nitin", balance: 5500.00 },
        { id: 102, name: "Ansh", balance: 6000.00 },
        { id: 103, name: "Megha", balance: 10500.00 }
    ];

    const transactions = [
        { id: 1009, sender: 101, receiver: 0, amount: 6000.00, status: "ROLLED_BACK", time: "Just now" },
        { id: 1008, sender: 101, receiver: 0, amount: 7000.00, status: "COMMITTED", time: "1 min ago" },
        { id: 1007, sender: 101, receiver: 0, amount: 2000.00, status: "COMMITTED", time: "2 mins ago" },
        { id: 1004, sender: 103, receiver: 101, amount: 500.00, status: "COMMITTED", time: "3 mins ago" },
        { id: 1003, sender: 102, receiver: 103, amount: 1000.00, status: "COMMITTED", time: "3 mins ago" },
        { id: 1002, sender: 101, receiver: 103, amount: 3000.00, status: "COMMITTED", time: "3 mins ago" },
        { id: 1001, sender: 101, receiver: 102, amount: 2000.00, status: "COMMITTED", time: "3 mins ago" }
    ];

    const threads = [
        { name: "T1", target: "Account 101 -> 102", state: "COMPLETED" },
        { name: "T2", target: "Account 101 -> 103", state: "COMPLETED" },
        { name: "T3", target: "Account 102 -> 103", state: "COMPLETED" },
        { name: "T4", target: "Account 103 -> 101", state: "COMPLETED" }
    ];

    function renderAccounts() {
        const container = document.getElementById('accounts-container');
        container.innerHTML = accounts.map(acc => `
            <div class="account-item">
                <div>
                    <span class="acc-id">Account ${acc.id}</span>
                    <span style="color: var(--text-secondary); margin-left: 8px;">(${acc.name})</span>
                </div>
                <div class="acc-balance">₹${acc.balance.toLocaleString('en-IN', { minimumFractionDigits: 2 })}</div>
            </div>
        `).join('');
    }

    function renderTransactions() {
        const tbody = document.getElementById('transactions-table-body');
        tbody.innerHTML = transactions.map(tx => `
            <tr>
                <td>#${tx.id}</td>
                <td>${tx.sender === 0 ? 'Cash/External' : 'Account ' + tx.sender}</td>
                <td>${tx.receiver === 0 ? 'Cash/External' : 'Account ' + tx.receiver}</td>
                <td>₹${tx.amount.toLocaleString('en-IN')}</td>
                <td>
                    <span class="badge ${tx.status === 'COMMITTED' ? 'badge-completed' : 'badge-failed'}">
                        ${tx.status}
                    </span>
                </td>
                <td>${tx.time}</td>
            </tr>
        `).join('');
    }

    function renderThreads() {
        const container = document.getElementById('threads-container');
        container.innerHTML = threads.map(th => `
            <div class="thread-row">
                <div>
                    <strong>[${th.name}]</strong> ${th.target}
                </div>
                <span class="badge badge-completed">${th.state}</span>
            </div>
        `).join('');
    }

    renderAccounts();
    renderTransactions();
    renderThreads();

    // Transfer simulation handler
    const form = document.getElementById('transfer-form');
    form.addEventListener('submit', (e) => {
        e.preventDefault();
        const sender = parseInt(document.getElementById('sender-select').value);
        const receiver = parseInt(document.getElementById('receiver-select').value);
        const amount = parseFloat(document.getElementById('transfer-amount').value);

        if (sender === receiver) {
            alert("Sender and receiver accounts must be different.");
            return;
        }

        const senderAcc = accounts.find(a => a.id === sender);
        const receiverAcc = accounts.find(a => a.id === receiver);

        const newId = transactions.length > 0 ? transactions[0].id + 1 : 1001;

        if (senderAcc.balance < amount) {
            transactions.unshift({
                id: newId,
                sender,
                receiver,
                amount,
                status: "ROLLED_BACK",
                time: "Just now"
            });
            alert(`Transaction #${newId} ROLLED_BACK: Insufficient balance in Account ${sender}!`);
        } else {
            senderAcc.balance -= amount;
            receiverAcc.balance += amount;
            transactions.unshift({
                id: newId,
                sender,
                receiver,
                amount,
                status: "COMMITTED",
                time: "Just now"
            });
        }

        renderAccounts();
        renderTransactions();
    });
});
