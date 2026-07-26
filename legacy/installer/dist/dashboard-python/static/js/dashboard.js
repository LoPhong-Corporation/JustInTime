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

function checkAlert(key, isOver, label) {
    if (isOver && !alertState[key]) {
        showToast(label);
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

const gaugeCpu = createGauge('gaugeCpu');
const gaugeRam = createGauge('gaugeRam');
const gaugeDisk = createGauge('gaugeDisk');
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
});

fetch('/api/system/network-interfaces').then(r => r.json()).then(list => {
    const tbody = document.getElementById('network-interfaces-body');
    if (!tbody) return;
    tbody.innerHTML = list.map(nic => `
        <tr>
            <td>${nic.name}</td>
            <td>${nic.ipv4}</td>
            <td style="color:${nic.is_up ? '#22c55e' : '#4a6584'}">${nic.is_up ? 'UP' : 'DOWN'}</td>
            <td>${nic.speed_mbps ? nic.speed_mbps + ' Mbps' : 'N/A'}</td>
        </tr>
    `).join('');
});

// ---------------- SSE: realtime system stats ----------------

const evtSource = new EventSource('/api/system/stream');
function setText(id, value) {
    const el = document.getElementById(id);
    if (el) el.textContent = value;
}

evtSource.onmessage = function(event) {
    let data;

    try {
        data = JSON.parse(event.data);
    } catch (e) {
        return;
    }

    if (data.error) return;

    try {
        // ---- Overview ----
        setText('cpu-value', data.cpu_percent.toFixed(1) + '%');
        setText('ram-value', data.ram_percent.toFixed(1) + '%');
        setText('disk-value', data.disk_percent.toFixed(1) + '%');
        setText('ram-sub', `${data.ram_used_gb} / ${data.ram_total_gb} GB`);
        setText('disk-sub', `${data.disk_used_gb} / ${data.disk_total_gb} GB`);

        updateGauge(gaugeCpu, data.cpu_percent, THRESHOLDS.cpu);
        updateGauge(gaugeRam, data.ram_percent, THRESHOLDS.ram);
        updateGauge(gaugeDisk, data.disk_percent, THRESHOLDS.disk);

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
            errorDiv.innerHTML = `<div class="error-banner">${summaryData.error}</div>`;
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
