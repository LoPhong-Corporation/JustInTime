// dashboard.js

// Dong bo font cua Chart.js voi font dang chon (--app-font),
// neu khong Chart.js se dung font mac dinh rieng, khong khop
// voi giao dien.
(function syncChartFont() {
    const appFont = getComputedStyle(document.documentElement)
        .getPropertyValue('--app-font')
        .trim();

    if (appFont && window.Chart) {
        Chart.defaults.font.family = appFont;
        Chart.defaults.color = '#7e9ac0';
    }
})();

function fmtDuration(sec) {
    sec = Math.round(sec);
    const h = Math.floor(sec / 3600);
    const m = Math.floor((sec % 3600) / 60);
    const s = sec % 60;
    return `${String(h).padStart(2,'0')}:${String(m).padStart(2,'0')}:${String(s).padStart(2,'0')}`;
}

function fmtBytesPerSec(bps) {
    if (bps > 1024 * 1024) return (bps / (1024*1024)).toFixed(2) + ' MB/s';
    if (bps > 1024) return (bps / 1024).toFixed(1) + ' KB/s';
    return bps.toFixed(0) + ' B/s';
}

function gaugeColor(value, threshold) {
    if (value >= threshold) return '#ef4444';
    if (value >= threshold * 0.75) return '#f5a524';
    return '#22c55e';
}

// ---------------- View switching (đọc ?view= từ URL) ----------------

function activateView(view) {
    document.querySelectorAll('.sidebar-view-link').forEach(l => {
        l.classList.toggle('active', l.dataset.view === view);
    });

    document.querySelectorAll('.dash-view').forEach(v => v.style.display = 'none');

    const target = document.getElementById(`view-${view}`);
    if (target) target.style.display = 'block';

    if (view === 'cloud') loadCloudData(currentRange);
    if (view === 'local') loadLocalData();
    if (view === 'devices') loadDevicesData();
    if (view === 'family') loadFamilyData();
}

// ---------------- Toast alerts ----------------

const alertState = { cpu: false, ram: false, disk: false };

function showToast(message) {
    const container = document.getElementById('toast-container');
    const toast = document.createElement('div');
    toast.className = 'toast';
    toast.textContent = message;
    container.appendChild(toast);
    setTimeout(() => toast.remove(), 6000);
}

const alertsLog = [];

function logAlert(label) {
    alertsLog.unshift({ label, time: new Date() });
    if (alertsLog.length > 20) alertsLog.pop();

    const list = document.getElementById('alerts-log');
    if (!list) return;

    list.innerHTML = alertsLog.map(a => `
        <li style="background:#000; border:1px solid #1c3a5e; border-radius:8px; padding:8px 12px; font-size:13px; display:flex; justify-content:space-between; gap:12px;">
            <span>⚠️ ${a.label}</span>
            <span style="color:#4a6584; white-space:nowrap;">${a.time.toLocaleTimeString()}</span>
        </li>
    `).join('');
}

function checkAlert(key, isOver, label) {
    if (isOver && !alertState[key]) {
        showToast(label);
        logAlert(label);
    }
    alertState[key] = isOver;
}

// ---------------- Radial gauges (Chart.js doughnut trick) ----------------

function createGauge(canvasId) {
    const el = document.getElementById(canvasId);
    if (!el) return null;
    const ctx = el.getContext('2d');
    return new Chart(ctx, {
        type: 'doughnut',
        data: {
            datasets: [{
                data: [0, 100],
                backgroundColor: ['#22c55e', '#12294a'],
                borderWidth: 0,
            }]
        },
        options: {
            rotation: -90,
            circumference: 180,
            cutout: '75%',
            animation: { duration: 400, easing: 'easeOutQuart' },
            plugins: { legend: { display: false }, tooltip: { enabled: false } }
        }
    });
}

function updateGauge(chart, value, threshold) {
    if (!chart) return;
    chart.data.datasets[0].data = [value, Math.max(100 - value, 0)];
    chart.data.datasets[0].backgroundColor[0] = gaugeColor(value, threshold);
    chart.update();
}

function createSparkline(canvasId, color) {
    const el = document.getElementById(canvasId);
    if (!el) return null;
    return new Chart(el.getContext('2d'), {
        type: 'line',
        data: { labels: [], datasets: [{ data: [], borderColor: color, backgroundColor: color + '33', fill: true, tension: 0.4, borderWidth: 2, pointRadius: 0 }] },
        options: {
            animation: false,
            interaction: { intersect: false },
            plugins: { legend: { display: false }, tooltip: { enabled: false } },
            scales: { x: { display: false }, y: { display: false, min: 0 } },
            elements: { line: { borderJoinStyle: 'round' } },
        }
    });
}

function pushSpark(chart, value) {
    if (!chart) return;
    chart.data.labels.push('');
    chart.data.datasets[0].data.push(value);
    if (chart.data.labels.length > 30) {
        chart.data.labels.shift();
        chart.data.datasets[0].data.shift();
    }
    chart.update('none');
}

const sparkCpu = createSparkline('sparkCpu', '#2f7de1');
const sparkRam = createSparkline('sparkRam', '#5aa9ff');
const sparkDisk = createSparkline('sparkDisk', '#f5a524');
const sparkNet = createSparkline('sparkNet', '#22c55e');

const gaugeCpuTab = createGauge('gaugeCpuTab');
const gaugeRamTab = createGauge('gaugeRamTab');

// ---------------- Line chart factory (smooth) ----------------

function createLineChart(canvasId, datasetsConfig, yOptions) {
    const el = document.getElementById(canvasId);
    if (!el) return null;

    return new Chart(el.getContext('2d'), {
        type: 'line',
        data: {
            labels: [],
            datasets: datasetsConfig.map(cfg => ({
                label: cfg.label,
                data: [],
                borderColor: cfg.color,
                backgroundColor: cfg.color + '22',
                fill: true,
                tension: 0.45,
                cubicInterpolationMode: 'monotone',
                borderWidth: 2,
                pointRadius: 0,
            }))
        },
        options: {
            animation: { duration: 400, easing: 'easeOutQuart' },
            interaction: { intersect: false },
            scales: {
                y: Object.assign({ ticks: { color: '#7e9ac0' }, grid: { color: '#1c3a5e' } }, yOptions || {}),
                x: { ticks: { color: '#7e9ac0', maxTicksLimit: 8 }, grid: { display: false } }
            },
            plugins: { legend: { labels: { color: '#dce8f7' } } }
        }
    });
}

function pushPoint(chart, label, values) {
    if (!chart) return;
    chart.data.labels.push(label);
    values.forEach((v, i) => chart.data.datasets[i].data.push(v));

    if (chart.data.labels.length > MAX_POINTS) {
        chart.data.labels.shift();
        chart.data.datasets.forEach(ds => ds.data.shift());
    }

    chart.update();
}

const MAX_POINTS = 60;

const historyChart = createLineChart('historyChart', [
    { label: 'CPU %', color: '#2f7de1' },
    { label: 'RAM %', color: '#5aa9ff' },
], { min: 0, max: 100 });

const cpuHistoryChart = createLineChart('cpuHistoryChart', [
    { label: 'CPU %', color: '#2f7de1' },
], { min: 0, max: 100 });

const ramHistoryChart = createLineChart('ramHistoryChart', [
    { label: 'RAM %', color: '#5aa9ff' },
    { label: 'Swap %', color: '#f5a524' },
], { min: 0, max: 100 });

const diskIoChart = createLineChart('diskIoChart', [
    { label: 'Read KB/s', color: '#22c55e' },
    { label: 'Write KB/s', color: '#ef4444' },
], {});

const netHistoryChart = createLineChart('netHistoryChart', [
    { label: 'Download KB/s', color: '#22c55e' },
    { label: 'Upload KB/s', color: '#f5a524' },
], {});

// ---------------- Machine info (once) ----------------

fetch('/api/system/info').then(r => r.json()).then(info => {
    const left = document.getElementById('machine-info-left');
    left.innerHTML = `
        <div class="info-row"><span class="label">Hostname</span><span class="value">${info.hostname}</span></div>
        <div class="info-row"><span class="label">OS</span><span class="value">${info.os}</span></div>
        <div class="info-row"><span class="label">Architecture</span><span class="value">${info.architecture}</span></div>
        <div class="info-row"><span class="label">Processor</span><span class="value">${info.processor}</span></div>
        <div class="info-row"><span class="label">CPU Cores</span><span class="value">${info.cpu_cores_physical} physical / ${info.cpu_cores_logical} logical</span></div>
        <div class="info-row"><span class="label">Total RAM</span><span class="value">${info.ram_total_gb} GB</span></div>
    `;

    const disksDiv = document.getElementById('machine-info-disks');
    disksDiv.innerHTML = info.disks.map(d => `
        <div class="info-row">
            <span class="label">${d.device} (${d.fstype})</span>
            <span class="value">${d.total_gb} GB</span>
        </div>
    `).join('');

    const cpuInfo = document.getElementById('cpu-info');
    if (cpuInfo) {
        cpuInfo.innerHTML = `
            <div class="info-row"><span class="label">Processor</span><span class="value">${info.processor}</span></div>
            <div class="info-row"><span class="label">Physical Cores</span><span class="value">${info.cpu_cores_physical}</span></div>
            <div class="info-row"><span class="label">Logical Cores</span><span class="value">${info.cpu_cores_logical}</span></div>
            <div class="info-row"><span class="label">Current Freq</span><span class="value">${info.cpu_freq_mhz || 'N/A'} MHz</span></div>
            <div class="info-row"><span class="label">Max Freq</span><span class="value">${info.cpu_freq_max_mhz || 'N/A'} MHz</span></div>
        `;
    }

    // Overview card subtitles (matches the reference layout: model/spec
    // shown under each card title).
    setText('card-cpu-subtitle', info.processor);
    setText('card-ram-subtitle', `${info.ram_total_gb} GB Total`);
    if (info.disks && info.disks.length) {
        setText('card-disk-subtitle', `${info.disks[0].total_gb} GB (${info.disks[0].fstype})`);
    }
});

fetch('/api/system/network-interfaces').then(r => r.json()).then(list => {
    const tbody = document.getElementById('network-interfaces-body');
    if (tbody) {
        tbody.innerHTML = list.map(nic => `
            <tr>
                <td>${nic.name}</td>
                <td>${nic.ipv4}</td>
                <td style="color:${nic.is_up ? '#22c55e' : '#4a6584'}">${nic.is_up ? 'UP' : 'DOWN'}</td>
                <td>${nic.speed_mbps ? nic.speed_mbps + ' Mbps' : 'N/A'}</td>
            </tr>
        `).join('');
    }

    const active = list.find(nic => nic.is_up && nic.ipv4 !== 'N/A');
    setText('card-net-subtitle', active ? active.name : 'No active adapter');
});

// ---------------- Processes panel ----------------

let allProcesses = [];

function renderProcesses(filter) {
    const tbody = document.getElementById('process-body');
    if (!tbody) return;

    const f = (filter || '').trim().toLowerCase();
    const rows = f ? allProcesses.filter(p => p.name.toLowerCase().includes(f)) : allProcesses;

    if (!rows.length) {
        tbody.innerHTML = `<tr><td colspan="5" style="color:#4a6584;">No matching processes.</td></tr>`;
        return;
    }

    tbody.innerHTML = rows.map(p => `
        <tr>
            <td>${p.pid}</td>
            <td class="proc-name">${p.name}</td>
            <td>${p.cpu_percent.toFixed(1)}%</td>
            <td>${p.mem_percent.toFixed(1)}%</td>
            <td><span class="proc-status">${p.status}</span></td>
        </tr>
    `).join('');
}

function loadProcesses() {
    fetch('/api/system/processes')
        .then(r => r.json())
        .then(data => {
            allProcesses = data.processes || [];
            renderProcesses(document.getElementById('global-search')?.value);
        })
        .catch(() => {});
}

document.getElementById('global-search')?.addEventListener('input', (e) => {
    renderProcesses(e.target.value);
});

loadProcesses();
setInterval(loadProcesses, 4000);

// ---------------- Machines (fleet overview via device_heartbeats) ----------------

function machineStatus(m) {
    const ageSec = (Date.now() - new Date(m.last_seen).getTime()) / 1000;
    if (ageSec > 90) return 'offline';

    const cpu = m.cpu_percent || 0, ram = m.ram_percent || 0, disk = m.disk_percent || 0;
    const critical = cpu >= (THRESHOLDS.cpu || 85) || ram >= (THRESHOLDS.ram || 85) || disk >= (THRESHOLDS.disk || 90);
    if (critical) return 'critical';

    const warning = cpu >= (THRESHOLDS.cpu || 85) - 10 || ram >= (THRESHOLDS.ram || 85) - 10 || disk >= (THRESHOLDS.disk || 90) - 10;
    if (warning) return 'warning';

    return 'online';
}

function timeAgo(iso) {
    const sec = Math.max(0, Math.round((Date.now() - new Date(iso).getTime()) / 1000));
    if (sec < 60) return `${sec}s ago`;
    if (sec < 3600) return `${Math.round(sec / 60)}m ago`;
    return `${Math.round(sec / 3600)}h ago`;
}

async function loadMachines() {
    const errorDiv = document.getElementById('machines-error');
    const listEl = document.getElementById('machines-list');
    errorDiv.innerHTML = '';

    try {
        const res = await fetch('/api/machines');
        const data = await res.json();

        if (data.error) {
            errorDiv.innerHTML = `<div class="error-banner">${data.error}</div>`;
            setFleetCounts({ online: 0, offline: 0, warning: 0, critical: 0 });
            return;
        }
        if (!data.logged_in) {
            errorDiv.innerHTML = `<div class="error-banner">${LABELS.loginRequired} — <a href="/login">${LABELS.login}</a></div>`;
            listEl.innerHTML = `<p style="color:#4a6584;">Log in to see every machine on your account here.</p>`;
            setFleetCounts({ online: 0, offline: 0, warning: 0, critical: 0 });
            return;
        }

        const machines = data.machines || [];
        const counts = { online: 0, offline: 0, warning: 0, critical: 0 };

        if (!machines.length) {
            listEl.innerHTML = `<p style="color:#4a6584;">No machines have reported in yet — this device will appear here shortly.</p>`;
        } else {
            listEl.innerHTML = machines.map(m => {
                const status = machineStatus(m);
                counts[status]++;
                const isSelf = m.device_id === data.self_device_id;

                return `
                    <div class="machine-card">
                        <div>
                            <div class="machine-name">${m.hostname || m.device_id}${isSelf ? ' (this device)' : ''}</div>
                            <div class="machine-sub">${m.device_id} — ${timeAgo(m.last_seen)}</div>
                        </div>
                        <div><span class="status-pill ${status}"><span class="dot"></span>${status}</span></div>
                        <div>
                            <div class="machine-metric-label">CPU</div>
                            <div class="machine-metric-value">${(m.cpu_percent || 0).toFixed(0)}%</div>
                            <div class="mini-bar"><div class="mini-bar-fill" style="width:${m.cpu_percent || 0}%; background:${gaugeColor(m.cpu_percent || 0, THRESHOLDS.cpu)}"></div></div>
                        </div>
                        <div>
                            <div class="machine-metric-label">RAM</div>
                            <div class="machine-metric-value">${(m.ram_percent || 0).toFixed(0)}%</div>
                            <div class="mini-bar"><div class="mini-bar-fill" style="width:${m.ram_percent || 0}%; background:${gaugeColor(m.ram_percent || 0, THRESHOLDS.ram)}"></div></div>
                        </div>
                        <div>
                            <div class="machine-metric-label">Disk</div>
                            <div class="machine-metric-value">${(m.disk_percent || 0).toFixed(0)}%</div>
                            <div class="mini-bar"><div class="mini-bar-fill" style="width:${m.disk_percent || 0}%; background:${gaugeColor(m.disk_percent || 0, THRESHOLDS.disk)}"></div></div>
                        </div>
                    </div>
                `;
            }).join('');
        }

        setFleetCounts(counts);
    } catch (e) {
        errorDiv.innerHTML = `<div class="error-banner">Connection error: ${e}</div>`;
    }
}

function setFleetCounts(counts) {
    setText('fleet-online-count', counts.online);
    setText('fleet-offline-count', counts.offline);
    setText('fleet-warning-count', counts.warning);
    setText('fleet-critical-count', counts.critical);
}

loadMachines();
setInterval(loadMachines, 30000);

// ---------------- Activity Timeline (this device, local) ----------------

async function loadActivityTimeline() {
    const el = document.getElementById('activity-timeline');
    if (!el) return;

    try {
        const res = await fetch('/api/local/recent');
        const data = await res.json();
        const recent = (data.recent || []).slice(0, 10);

        if (!recent.length) {
            el.innerHTML = `<p style="color:#4a6584; font-size:13px;">No local activity recorded yet.</p>`;
            return;
        }

        el.innerHTML = recent.map(a => `
            <div class="timeline-item">
                <div class="timeline-time">${new Date(a.start_time * 1000).toLocaleString()}</div>
                <div class="timeline-title">${a.process_name}</div>
                <div class="timeline-sub">${(a.window_title || '').substring(0, 70)} — ${fmtDuration(a.duration_seconds)}</div>
            </div>
        `).join('');
    } catch (e) {
        el.innerHTML = `<p style="color:#4a6584; font-size:13px;">Could not load activity timeline.</p>`;
    }
}

loadActivityTimeline();
setInterval(loadActivityTimeline, 30000);

// ---------------- Agent / Service Status ----------------

async function loadAgentStatus() {
    const el = document.getElementById('agent-status-body');
    if (!el) return;

    try {
        const res = await fetch('/api/local/recent');
        const data = await res.json();
        const recent = data.recent || [];

        let agentRow;
        if (recent.length > 0) {
            const mostRecentEnd = recent[0].end_time || recent[0].start_time;
            const ageSec = (Date.now() / 1000) - mostRecentEnd;
            const isActive = ageSec < 300; // 5 phút

            agentRow = `
                <div class="agent-status-row">
                    <span class="label">Local tracking (agent.exe)</span>
                    <span class="value"><span class="status-pill ${isActive ? 'online' : 'warning'}"><span class="dot"></span>${isActive ? 'Active' : 'Idle'}</span></span>
                </div>
                <div class="agent-status-row">
                    <span class="label">Last record written</span>
                    <span class="value">${timeAgo(new Date(mostRecentEnd * 1000).toISOString())}</span>
                </div>
            `;
        } else {
            agentRow = `
                <div class="agent-status-row">
                    <span class="label">Local tracking (agent.exe)</span>
                    <span class="value"><span class="status-pill offline"><span class="dot"></span>No data</span></span>
                </div>
            `;
        }

        const statusRes = await fetch('/api/auth/status');
        const statusData = await statusRes.json();

        el.innerHTML = agentRow + `
            <div class="agent-status-row">
                <span class="label">Dashboard mode</span>
                <span class="value">${statusData.logged_in ? 'Online (cloud sync)' : 'Offline (local only)'}</span>
            </div>
            <div class="agent-status-row">
                <span class="label">Device ID</span>
                <span class="value">${DEVICE_ID || '—'}</span>
            </div>
        `;
    } catch (e) {
        el.innerHTML = `<div class="agent-status-row"><span class="label">Could not load status</span><span class="value"></span></div>`;
    }
}

loadAgentStatus();
setInterval(loadAgentStatus, 15000);

// ---------------- SSE: realtime system stats ----------------

const evtSource = new EventSource('/api/system/stream');
function setText(id, value) {
    const el = document.getElementById(id);
    if (el) el.textContent = value;
}

evtSource.onopen = function() {
    const badge = document.getElementById('status-badge');
    if (badge) badge.classList.remove('offline');
    setText('status-text', 'Running');
};

evtSource.onerror = function() {
    const badge = document.getElementById('status-badge');
    if (badge) badge.classList.add('offline');
    setText('status-text', 'Disconnected');
};

evtSource.onmessage = function(event) {
    let data;

    try {
        data = JSON.parse(event.data);
    } catch (e) {
        return;
    }

    if (data.error) return;

    try {
        // ---- Overview cards ----
        setText('card-cpu-percent', data.cpu_percent.toFixed(1) + '%');
        setText('card-ram-percent', data.ram_percent.toFixed(1) + '%');
        setText('card-disk-percent', data.disk_percent.toFixed(1) + '%');
        setText('card-net-percent', fmtBytesPerSec(data.net_download_bps));

        setText('card-ram-subtitle', `${data.ram_used_gb} / ${data.ram_total_gb} GB used`);
        setText('card-disk-subtitle', `${data.disk_used_gb} / ${data.disk_total_gb} GB used`);

        setText('card-cpu-updated', 'Updated just now');
        setText('card-ram-updated', 'Updated just now');
        setText('card-disk-updated', 'Updated just now');
        setText('card-net-updated', 'Updated just now');
        setText('history-updated', 'Updated just now');

        pushSpark(sparkCpu, data.cpu_percent);
        pushSpark(sparkRam, data.ram_percent);
        pushSpark(sparkDisk, data.disk_percent);
        pushSpark(sparkNet, data.net_download_bps / 1024);

        // ---- CPU tab ----
        setText('cpu-value-tab', data.cpu_percent.toFixed(1) + '%');
        updateGauge(gaugeCpuTab, data.cpu_percent, THRESHOLDS.cpu);

        const coresGrid = document.getElementById('cpu-cores-grid');
        if (coresGrid && data.cpu_per_core) {
            coresGrid.innerHTML = data.cpu_per_core.map((v, i) => `
                <div class="core-item">
                    <div class="core-label">Core ${i}</div>
                    <div class="core-percent">${v.toFixed(0)}%</div>
                    <div class="gauge-bar"><div class="gauge-fill" style="width:${v}%; background:${gaugeColor(v, THRESHOLDS.cpu)}"></div></div>
                </div>
            `).join('');
        }

        // ---- RAM tab ----
        setText('ram-value-tab', data.ram_percent.toFixed(1) + '%');
        updateGauge(gaugeRamTab, data.ram_percent, THRESHOLDS.ram);

        const ramInfo = document.getElementById('ram-info');
        if (ramInfo) {
            ramInfo.innerHTML = `
                <div class="info-row"><span class="label">Used</span><span class="value">${data.ram_used_gb} GB</span></div>
                <div class="info-row"><span class="label">Available</span><span class="value">${data.ram_available_gb} GB</span></div>
                <div class="info-row"><span class="label">Total</span><span class="value">${data.ram_total_gb} GB</span></div>
                <div class="info-row"><span class="label">Swap Used</span><span class="value">${data.swap_used_gb} / ${data.swap_total_gb} GB (${data.swap_percent}%)</span></div>
            `;
        }

        // ---- Disk tab ----
        const diskPartitions = document.getElementById('disk-partitions');
        if (diskPartitions && data.disks) {
            diskPartitions.innerHTML = data.disks.map(d => `
                <div style="margin-bottom:14px;">
                    <div class="info-row"><span class="label">${d.mountpoint}</span><span class="value">${d.used_gb} / ${d.total_gb} GB (${d.percent}%)</span></div>
                    <div class="gauge-bar"><div class="gauge-fill" style="width:${d.percent}%; background:${gaugeColor(d.percent, THRESHOLDS.disk)}"></div></div>
                </div>
            `).join('');
        }

        // ---- Network ----
        setText('net-down-tab', fmtBytesPerSec(data.net_download_bps));
        setText('net-up-tab', fmtBytesPerSec(data.net_upload_bps));

        // ---- Alerts ----
        checkAlert('cpu', data.cpu_alert, LABELS.cpuHigh + `: ${data.cpu_percent.toFixed(0)}%`);
        checkAlert('ram', data.ram_alert, LABELS.ramHigh + `: ${data.ram_percent.toFixed(0)}%`);
        checkAlert('disk', data.disk_alert, LABELS.diskHigh + `: ${data.disk_percent.toFixed(0)}%`);
    } catch (e) {
        console.error('Error updating widgets:', e);
    }

    // Cap nhat bieu do NAM NGOAI try/catch tren de dam bao
    // 1 loi UI nho khong bao gio lam gian doan viec ve chart.
    try {
        const t = new Date(data.timestamp * 1000).toLocaleTimeString();

        pushPoint(historyChart, t, [data.cpu_percent, data.ram_percent]);
        pushPoint(cpuHistoryChart, t, [data.cpu_percent]);
        pushPoint(ramHistoryChart, t, [data.ram_percent, data.swap_percent]);
        pushPoint(diskIoChart, t, [data.disk_read_bps / 1024, data.disk_write_bps / 1024]);
        pushPoint(netHistoryChart, t, [data.net_download_bps / 1024, data.net_upload_bps / 1024]);
    } catch (e) {
        console.error('Error updating charts:', e);
    }
};

// ---------------- Cloud data (from Supabase) ----------------

let currentRange = 'today';
let cloudChart = null;
let dailyBarChart = null;

document.querySelectorAll('.range-btn').forEach(btn => {
    btn.addEventListener('click', () => {
        document.querySelectorAll('.range-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentRange = btn.dataset.range;
        loadCloudData(currentRange);
    });
});

document.getElementById('btn-export-csv')?.addEventListener('click', (e) => {
    e.preventDefault();
    window.location.href = `/export/csv?range=${currentRange}`;
});

document.getElementById('btn-print-report')?.addEventListener('click', (e) => {
    e.preventDefault();
    window.open(`/report?range=${currentRange}`, '_blank');
});

async function loadCloudData(range) {
    const errorDiv = document.getElementById('cloud-error');
    errorDiv.innerHTML = '';

    try {
        const summaryRes = await fetch(`/api/cloud/summary?range=${range}`);
        const summaryData = await summaryRes.json();

        if (summaryData.error) {
            errorDiv.innerHTML = `<div class="error-banner">${summaryData.error} — <a href="/login">${LABELS.login}</a></div>`;
            return;
        }

        const apps = summaryData.apps || [];
        const ctx = document.getElementById('cloudChart').getContext('2d');
        if (cloudChart) cloudChart.destroy();

        cloudChart = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: apps.map(a => a.process_name),
                datasets: [{ label: 'Seconds', data: apps.map(a => a.total_seconds), backgroundColor: '#2f7de1', borderRadius: 6 }]
            },
            options: {
                indexAxis: 'y',
                plugins: { legend: { display: false } },
                scales: {
                    x: { ticks: { color: '#7e9ac0' }, grid: { color: '#1c3a5e' } },
                    y: { ticks: { color: '#dce8f7' }, grid: { display: false } }
                }
            }
        });

        const dailyRes = await fetch(`/api/cloud/daily?range=${range}`);
        const dailyData = await dailyRes.json();

        const dailyCtx = document.getElementById('dailyBarChart').getContext('2d');
        if (dailyBarChart) dailyBarChart.destroy();

        dailyBarChart = new Chart(dailyCtx, {
            type: 'bar',
            data: {
                labels: dailyData.days || [],
                datasets: [{ label: 'Total seconds', data: dailyData.totals || [], backgroundColor: '#5aa9ff', borderRadius: 6 }]
            },
            options: {
                plugins: { legend: { display: false } },
                scales: {
                    x: { ticks: { color: '#7e9ac0' }, grid: { display: false } },
                    y: { ticks: { color: '#7e9ac0' }, grid: { color: '#1c3a5e' } }
                }
            }
        });

        const recentRes = await fetch('/api/cloud/recent');
        const recentData = await recentRes.json();

        if (recentData.error) {
            errorDiv.innerHTML = `<div class="error-banner">${recentData.error}</div>`;
            return;
        }

        const tbody = document.getElementById('cloud-recent-body');
        tbody.innerHTML = (recentData.records || []).slice(0, 50).map(r => `
            <tr>
                <td>${r.device_id}</td>
                <td>${r.process_name}</td>
                <td>${(r.window_title || '').substring(0, 60)}</td>
                <td>${fmtDuration(r.duration_seconds)}</td>
                <td>${new Date(r.start_time * 1000).toLocaleString()}</td>
            </tr>
        `).join('');

    } catch (e) {
        errorDiv.innerHTML = `<div class="error-banner">Connection error: ${e}</div>`;
    }
}

// ---------------- Khởi tạo view ban đầu (đọc ?view= từ URL) ----------------
// Đặt CUỐI file vì cần currentRange/loadCloudData đã được khai báo ở trên.

const initialView = new URLSearchParams(window.location.search).get('view') || 'overview';
activateView(initialView);

// ---------------- Local Activity (this device's own SQLite, works offline) ----------------

let localChart = null;

async function loadLocalData() {
    const errorDiv = document.getElementById('local-error');
    if (errorDiv) errorDiv.innerHTML = '';

    try {
        const summaryRes = await fetch('/api/local/summary');
        const summaryData = await summaryRes.json();

        if (summaryData.error) {
            if (errorDiv) errorDiv.innerHTML = `<div class="error-banner">${summaryData.error}</div>`;
            return;
        }

        const usage = summaryData.usage || [];
        const ctx = document.getElementById('localChart').getContext('2d');
        if (localChart) localChart.destroy();

        localChart = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: usage.map(u => u.process_name),
                datasets: [{ label: 'Seconds', data: usage.map(u => u.total_seconds), backgroundColor: '#22c55e', borderRadius: 6 }]
            },
            options: {
                indexAxis: 'y',
                plugins: { legend: { display: false } },
                scales: {
                    x: { ticks: { color: '#7e9ac0' }, grid: { color: '#1c3a5e' } },
                    y: { ticks: { color: '#dce8f7' }, grid: { display: false } }
                }
            }
        });

        const recentRes = await fetch('/api/local/recent');
        const recentData = await recentRes.json();

        const tbody = document.getElementById('local-recent-body');
        tbody.innerHTML = (recentData.recent || []).map(a => `
            <tr>
                <td>${a.process_name}</td>
                <td>${(a.window_title || '').substring(0, 60)}</td>
                <td>${fmtDuration(a.duration_seconds)}</td>
                <td>${new Date(a.start_time * 1000).toLocaleString()}</td>
            </tr>
        `).join('');
    } catch (e) {
        if (errorDiv) errorDiv.innerHTML = `<div class="error-banner">Connection error: ${e}</div>`;
    }
}

// ---------------- Devices (cross-device messaging by device_id, relayed via Supabase) ----------------

document.getElementById('send-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    const target_device_id = document.getElementById('target-device').value;
    const kind = document.getElementById('msg-kind').value;
    const payload = document.getElementById('msg-payload').value;
    if (!target_device_id) return;

    try {
        await fetch('/api/send', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ target_device_id, kind, payload }),
        });
        document.getElementById('msg-payload').value = '';
        loadDevicesData();
    } catch (e) {
        // ignore — inbox refresh will surface any lingering error state
    }
});

async function loadDevicesData() {
    const errorDiv = document.getElementById('devices-error');
    errorDiv.innerHTML = '';

    try {
        const devRes = await fetch('/api/devices');
        const devData = await devRes.json();

        const chips = document.getElementById('devices-chips');
        const select = document.getElementById('target-device');
        chips.innerHTML = '';
        select.innerHTML = '';

        if (!devData.logged_in) {
            errorDiv.innerHTML = `<div class="error-banner">${LABELS.loginRequired} — <a href="/login">${LABELS.login}</a></div>`;
        } else if (!devData.devices || devData.devices.length === 0) {
            chips.innerHTML = `<span style="color:#4a6584; font-size:13px;">${LABELS.noDevices}</span>`;
        } else {
            devData.devices.forEach(d => {
                const chip = document.createElement('span');
                chip.style.cssText = 'background:#000;border:1px solid #1c3a5e;padding:4px 10px;border-radius:999px;font-size:12px;';
                chip.textContent = d;
                chips.appendChild(chip);

                const opt = document.createElement('option');
                opt.value = d;
                opt.textContent = d;
                select.appendChild(opt);
            });
        }

        const inboxRes = await fetch('/api/inbox');
        const inboxData = await inboxRes.json();
        const ul = document.getElementById('devices-inbox');

        const messages = inboxData.messages || [];
        ul.innerHTML = messages.length
            ? messages.map(m => `
                <li style="background:#000;border:1px solid #1c3a5e;border-radius:8px;padding:8px 12px;font-size:13px;">
                    <strong>${m.sender_device_id}</strong> (${m.kind}): ${m.payload}
                </li>
            `).join('')
            : `<li style="color:#4a6584; list-style:none;">${LABELS.noMessages}</li>`;
    } catch (e) {
        errorDiv.innerHTML = `<div class="error-banner">Connection error: ${e}</div>`;
    }
}

// ---------------- Family (consent-based parent/child linking + app limits) ----------------

let familySelectedChildId = null;
let familyChart = null;

async function loadFamilyData() {
    await Promise.all([loadFamilyChildren(), loadFamilyParents()]);
}

document.getElementById('invite-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();
    const errorDiv = document.getElementById('family-invite-error');
    errorDiv.innerHTML = '';

    const emailInput = document.getElementById('invite-email');
    const child_email = emailInput.value.trim();
    if (!child_email) return;

    try {
        const res = await fetch('/api/parent/invite', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ child_email }),
        });
        const data = await res.json();

        if (data.ok) {
            emailInput.value = '';
            loadFamilyChildren();
        } else {
            errorDiv.innerHTML = `<div class="error-banner">${data.error || 'Request failed'}</div>`;
        }
    } catch (e) {
        errorDiv.innerHTML = `<div class="error-banner">Connection error: ${e}</div>`;
    }
});

function familyStatusLabel(status) {
    if (status === 'pending') return LABELS.familyStatusPending;
    if (status === 'approved') return LABELS.familyStatusApproved;
    return LABELS.familyStatusRevoked;
}

async function loadFamilyChildren() {
    const errorDiv = document.getElementById('family-children-error');
    errorDiv.innerHTML = '';

    try {
        const res = await fetch('/api/parent/children');
        const data = await res.json();

        if (data.error) {
            errorDiv.innerHTML = `<div class="error-banner">${data.error}</div>`;
            return;
        }

        if (!data.logged_in) {
            errorDiv.innerHTML = `<div class="error-banner">${LABELS.loginRequired} — <a href="/login">${LABELS.login}</a></div>`;
            return;
        }

        const links = data.links || [];
        const tbody = document.getElementById('family-children-body');

        if (!links.length) {
            tbody.innerHTML = `<tr><td colspan="3" style="color:#4a6584;">${LABELS.familyNoChildren}</td></tr>`;
            return;
        }

        tbody.innerHTML = links.map(l => `
            <tr>
                <td>${l.other_email}</td>
                <td>${familyStatusLabel(l.status)}</td>
                <td>
                    ${l.status !== 'revoked' ? `<button type="button" class="btn btn-outline" onclick="familySelectChild('${l.other_user_id}', this)">${l.status === 'approved' ? '→' : '...'}</button>` : ''}
                    ${l.status !== 'revoked' ? `<button type="button" class="btn btn-outline" onclick="familyRevokeLink(${l.id})">${LABELS.familyRevoke}</button>` : ''}
                </td>
            </tr>
        `).join('');
    } catch (e) {
        errorDiv.innerHTML = `<div class="error-banner">Connection error: ${e}</div>`;
    }
}

async function familyRevokeLink(id) {
    try {
        await fetch('/api/parent/revoke', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id }),
        });
        loadFamilyChildren();
        loadFamilyParents();
    } catch (e) {}
}

async function familyApproveLink(id) {
    try {
        await fetch('/api/parent/approve', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id }),
        });
        loadFamilyParents();
    } catch (e) {}
}

function familySelectChild(childUserId, btn) {
    familySelectedChildId = childUserId;
    document.getElementById('family-limits-hint').textContent = '';
    loadFamilyLimits();
    loadFamilyChildSummary();
}

async function loadFamilyLimits() {
    const tbody = document.getElementById('family-limits-body');
    const hint = document.getElementById('family-limits-hint');

    if (!familySelectedChildId) {
        hint.textContent = LABELS.familySelectChild;
        tbody.innerHTML = '';
        return;
    }

    try {
        const res = await fetch(`/api/parent/limits?child_id=${encodeURIComponent(familySelectedChildId)}`);
        const data = await res.json();

        if (data.error) {
            hint.textContent = data.error;
            tbody.innerHTML = '';
            return;
        }

        hint.textContent = '';
        const limits = data.limits || [];

        tbody.innerHTML = limits.length
            ? limits.map(l => `
                <tr>
                    <td>${l.process_name}</td>
                    <td>${l.blocked ? '—' : (l.daily_limit_sec != null ? Math.round(l.daily_limit_sec / 60) + ' min/day' : LABELS.familyNoLimitText)}</td>
                    <td><button type="button" class="btn btn-outline" onclick="familyDeleteLimit(${l.id})">${LABELS.familyDeleteLimit}</button></td>
                </tr>
            `).join('')
            : `<tr><td colspan="3" style="color:#4a6584;">${LABELS.familyNoLimits}</td></tr>`;
    } catch (e) {
        hint.textContent = `Connection error: ${e}`;
    }
}

document.getElementById('limit-form')?.addEventListener('submit', async (e) => {
    e.preventDefault();

    if (!familySelectedChildId) {
        document.getElementById('family-limits-hint').textContent = LABELS.familySelectChild;
        return;
    }

    const process_name = document.getElementById('limit-process').value.trim();
    if (!process_name) return;

    const noLimit = document.getElementById('limit-nolimit').checked;
    const blocked = document.getElementById('limit-block').checked;
    const minutes = parseInt(document.getElementById('limit-minutes').value, 10) || 60;

    try {
        await fetch('/api/parent/limits/set', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                child_user_id: familySelectedChildId,
                process_name,
                daily_limit_min: (noLimit || blocked) ? null : minutes,
                blocked,
            }),
        });
        document.getElementById('limit-process').value = '';
        loadFamilyLimits();
    } catch (e) {}
});

async function familyDeleteLimit(id) {
    try {
        await fetch('/api/parent/limits/delete', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id }),
        });
        loadFamilyLimits();
    } catch (e) {}
}

async function loadFamilyChildSummary() {
    if (!familySelectedChildId) return;

    try {
        const res = await fetch(`/api/parent/child-summary?child_id=${encodeURIComponent(familySelectedChildId)}`);
        const data = await res.json();

        const apps = data.apps || [];
        const ctx = document.getElementById('familyChart').getContext('2d');
        if (familyChart) familyChart.destroy();

        familyChart = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: apps.map(a => a.process_name),
                datasets: [{ label: 'Seconds', data: apps.map(a => a.total_seconds), backgroundColor: '#f5a524', borderRadius: 6 }]
            },
            options: {
                indexAxis: 'y',
                plugins: { legend: { display: false } },
                scales: {
                    x: { ticks: { color: '#7e9ac0' }, grid: { color: '#1c3a5e' } },
                    y: { ticks: { color: '#dce8f7' }, grid: { display: false } }
                }
            }
        });

        const tbody = document.getElementById('family-recent-body');
        const recent = data.recent || [];
        tbody.innerHTML = recent.length
            ? recent.map(a => `
                <tr>
                    <td>${a.process_name}</td>
                    <td>${(a.window_title || '').substring(0, 60)}</td>
                    <td>${fmtDuration(a.duration_seconds)}</td>
                    <td>${new Date(a.start_time * 1000).toLocaleString()}</td>
                </tr>
            `).join('')
            : '';
    } catch (e) {}
}

async function loadFamilyParents() {
    const errorDiv = document.getElementById('family-parents-error');
    errorDiv.innerHTML = '';

    try {
        const res = await fetch('/api/parent/parents');
        const data = await res.json();

        if (data.error) {
            errorDiv.innerHTML = `<div class="error-banner">${data.error}</div>`;
            return;
        }

        if (!data.logged_in) {
            errorDiv.innerHTML = `<div class="error-banner">${LABELS.loginRequired} — <a href="/login">${LABELS.login}</a></div>`;
            return;
        }

        const links = data.links || [];
        const tbody = document.getElementById('family-parents-body');

        if (!links.length) {
            tbody.innerHTML = `<tr><td colspan="3" style="color:#4a6584;">${LABELS.familyNoParents}</td></tr>`;
            return;
        }

        tbody.innerHTML = links.map(l => `
            <tr>
                <td>${l.other_email}</td>
                <td>${familyStatusLabel(l.status)}</td>
                <td>
                    ${l.status === 'pending' ? `<button type="button" class="btn" onclick="familyApproveLink(${l.id})">${LABELS.familyApprove}</button>` : ''}
                    ${l.status !== 'revoked' ? `<button type="button" class="btn btn-outline" onclick="familyRevokeLink(${l.id})">${LABELS.familyRevoke}</button>` : ''}
                </td>
            </tr>
        `).join('');
    } catch (e) {
        errorDiv.innerHTML = `<div class="error-banner">Connection error: ${e}</div>`;
    }
}

// ---------------- AI Insights (on-demand, opt-in) ----------------

document.getElementById('btn-generate-insights')?.addEventListener('click', async () => {
    const btn = document.getElementById('btn-generate-insights');
    const errorDiv = document.getElementById('insights-error');
    const loadingDiv = document.getElementById('insights-loading');
    const resultDiv = document.getElementById('insights-result');

    errorDiv.innerHTML = '';
    resultDiv.style.display = 'none';
    loadingDiv.style.display = 'block';
    btn.disabled = true;

    try {
        const res = await fetch('/api/local/insights');
        const data = await res.json();

        loadingDiv.style.display = 'none';
        btn.disabled = false;

        if (data.error) {
            errorDiv.innerHTML = `<div class="error-banner">${data.error}</div>`;
            return;
        }

        document.getElementById('insights-summary').textContent = data.summary || '';

        const appsBody = document.getElementById('insights-apps-body');
        appsBody.innerHTML = (data.apps || []).map(a => `
            <tr>
                <td>${a.process_name}</td>
                <td>${a.category}</td>
                <td>${a.note || ''}</td>
            </tr>
        `).join('');

        const recList = document.getElementById('insights-recommendations');
        recList.innerHTML = (data.recommendations || []).map(r => `<li>${r}</li>`).join('');

        resultDiv.style.display = 'block';
    } catch (e) {
        loadingDiv.style.display = 'none';
        btn.disabled = false;
        errorDiv.innerHTML = `<div class="error-banner">Connection error: ${e}</div>`;
    }
});
