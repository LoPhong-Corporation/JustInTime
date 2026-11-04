(() => {
    const root = document.body;
    const titles = {
        overview: ['Overview', 'YOUR SCREEN TIME', 'A clear view of how you spend time on this device.'],
        apps: ['Applications', 'APP USAGE', 'See which applications account for your tracked time.'],
        history: ['History', 'ACTIVITY HISTORY', 'Review tracked activity over time.'],
        cloud: ['Synced activity', 'ACCOUNT ACTIVITY', 'Review usage uploaded from your connected devices.'],
        insights: ['AI insights & chat', 'PERSONAL INSIGHTS', 'Explore your usage with optional, on-demand AI tools.'],
        limits: ['App limits', 'APPLICATION LIMITS', 'Review limits for connected family accounts.'],
        devices: ['Devices', 'CONNECTED DEVICES', 'View devices connected to your account.'],
        family: ['Family & limits', 'FAMILY CONTROLS', 'Manage family connections and application limits.'],
    };
    const colors = ['#0b7a75', '#3b6fd4', '#e0a21b', '#c8431f', '#8a63c9', '#7b8794'];
    const state = {
        view: 'overview',
        usage: [],
        recent: [],
        period: root.dataset.defaultPeriod || 'today',
        customFrom: '',
        customTo: '',
        children: [],
        selectedChild: '',
        selectedPermission: 'full',
        selectedLimitsChild: '',
        selectedLimitsPermission: 'full',
        cloudRange: 'week',
        selectedApp: '',
        deviceID: '',
        focusEndAt: Number(localStorage.getItem('jit-focus-end')) || 0,
        breakReminder: localStorage.getItem('jit-break-reminder') !== 'off',
        chatHistory: [],
    };
    const dailyGoalMin = Math.min(1440, Math.max(30, Number(root.dataset.goalMin) || 480));

    const el = (id) => document.getElementById(id);
    const currentTheme = () => root.dataset.theme || (window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light');
    const themeIcon = (theme) => theme === 'dark'
        ? '<svg class="time-icon" viewBox="0 0 24 24" aria-hidden="true"><path d="M20.2 15.1A8.5 8.5 0 0 1 8.9 3.8 8.7 8.7 0 1 0 20.2 15.1Z"/></svg>'
        : '<svg class="time-icon" viewBox="0 0 24 24" aria-hidden="true"><circle cx="12" cy="12" r="4"/><path d="M12 2v2m0 16v2M4.93 4.93l1.42 1.42m11.3 11.3 1.42 1.42M2 12h2m16 0h2M4.93 19.07l1.42-1.42m11.3-11.3 1.42-1.42"/></svg>';
    const escapeHTML = (value) => String(value ?? '').replace(/[&<>"']/g, (char) => ({
        '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;',
    })[char]);
    const formatDuration = (seconds) => {
        const total = Math.max(0, Math.round(Number(seconds) || 0));
        const hours = Math.floor(total / 3600);
        const minutes = Math.floor((total % 3600) / 60);
        if (hours) return `${hours}h ${minutes}m`;
        if (minutes) return `${minutes}m`;
        return `${total}s`;
    };
    const formatDateTime = (seconds) => new Date(Number(seconds) * 1000).toLocaleString([], {
        month: 'short',
        day: 'numeric',
        hour: '2-digit',
        minute: '2-digit',
        hour12: root.dataset.timeFormat === '12h',
    });
    const formatDateKey = (date) => `${date.getFullYear()}-${String(date.getMonth() + 1).padStart(2, '0')}-${String(date.getDate()).padStart(2, '0')}`;
    const formatDay = (dateString) => new Date(`${dateString}T12:00:00`).toLocaleDateString([], { weekday: 'short' });

    async function requestJSON(url, options) {
        const response = await fetch(url, options);
        let data;
        try {
            data = await response.json();
        } catch {
            throw new Error(`The server returned an invalid response (${response.status}).`);
        }
        if (!response.ok || data.error) throw new Error(data.error || `Request failed (${response.status}).`);
        return data;
    }

    function showNotice(message, isError = true) {
        const notice = el('app-notice');
        notice.textContent = message;
        notice.hidden = !message;
        notice.classList.toggle('is-error', isError);
    }

    function showInline(id, message) {
        const node = el(id);
        node.textContent = message;
        node.hidden = !message;
    }

    function setView(view, updateURL = true) {
        if (!Object.prototype.hasOwnProperty.call(titles, view)) view = 'overview';
        state.view = view;
        document.querySelectorAll('.time-view').forEach((section) => {
            section.hidden = section.id !== `view-${view}`;
            section.classList.remove('is-entering');
        });
        const activeSection = el(`view-${view}`);
        void activeSection.offsetWidth;
        activeSection.classList.add('is-entering');
        document.querySelectorAll('.time-nav-link[data-view]').forEach((link) => {
            const active = link.dataset.view === view;
            link.classList.toggle('active', active);
            if (active) link.setAttribute('aria-current', 'page');
            else link.removeAttribute('aria-current');
        });
        const [title, eyebrow, subtitle] = titles[view];
        el('page-title').textContent = title;
        el('page-eyebrow').textContent = eyebrow;
        el('page-subtitle').textContent = subtitle;
        showNotice('');
        if (updateURL) history.replaceState(null, '', `/?view=${encodeURIComponent(view)}`);
        if (view === 'overview') loadOverview();
        if (view === 'apps') loadApplications();
        if (view === 'history') loadHistory(state.period);
        if (view === 'cloud') loadCloud();
        if (view === 'limits') loadLimitsPage();
        if (view === 'devices') loadDevices();
        if (view === 'family') loadFamily();
    }

    function renderAppRows(apps, container, limit = Infinity, clickable = false) {
        const rows = apps.slice(0, limit);
        if (!rows.length) {
            container.innerHTML = '<div class="time-empty">No activity recorded for this period.</div>';
            return;
        }
        const max = Math.max(...rows.map((app) => Number(app.total_seconds) || 0), 1);
        container.innerHTML = rows.map((app, index) => {
            const name = String(app.process_name || 'Unknown application');
            const seconds = Number(app.total_seconds) || 0;
            const percent = Math.max(2, Math.min(100, seconds / max * 100));
            const color = colors[index % colors.length];
            const row = `<span class="time-app-icon" style="--app-color:${color}" aria-hidden="true"><svg viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="16" rx="2"/><path d="M3 9h18m-13 4h3m-3 3h7"/></svg></span>
                <span class="time-app-details">
                    <span class="time-app-name">${escapeHTML(name)}</span>
                    <span class="time-app-meter"><span style="width:${percent}%;background:${color}"></span></span>
                </span>
                <span class="time-app-duration">${formatDuration(seconds)}</span>`;
            return clickable ? `<button type="button" class="time-app-row time-app-row-button" data-process-name="${escapeHTML(name)}">${row}</button>` : `<div class="time-app-row">
                ${row}
            </div>`;
        }).join('');
    }

    function renderRecent(rows, targetID, limit = 8) {
        const target = el(targetID);
        if (!rows.length) {
            target.innerHTML = '<tr><td colspan="4" class="time-empty">No activity recorded yet.</td></tr>';
            return;
        }
        target.innerHTML = rows.slice(0, limit).map((activity) => `
            <tr>
                <td>${escapeHTML(activity.process_name || 'Unknown application')}</td>
                <td class="time-cell-muted" title="${escapeHTML(activity.window_title || '')}">${escapeHTML(activity.window_title || '—')}</td>
                <td>${formatDuration(activity.duration_seconds)}</td>
                <td class="time-cell-muted">${escapeHTML(formatDateTime(activity.start_time))}</td>
            </tr>`).join('');
    }

    function renderWeek(container, dailyTotals, highlightToday) {
        if (!dailyTotals.length) {
            container.innerHTML = '<div class="time-empty">No daily totals available.</div>';
            return;
        }
        const max = Math.max(...dailyTotals.map((day) => Number(day.total_seconds) || 0), 1);
        const today = formatDateKey(new Date());
        container.innerHTML = dailyTotals.map((day) => {
            const seconds = Number(day.total_seconds) || 0;
            const height = Math.max(seconds ? 3 : 0, seconds / max * 100);
            const isToday = highlightToday && day.date === today;
            return `<div class="time-week-column${isToday ? ' is-today' : ''}">
                <span class="time-week-value">${seconds ? escapeHTML(formatDuration(seconds)) : ''}</span>
                <span class="time-week-track"><span class="time-week-bar" style="height:${height}%"></span></span>
                <span>${escapeHTML(formatDay(day.date))}</span>
            </div>`;
        }).join('');
    }

    function renderCurrentActivity(rows) {
        const current = rows.find((activity) => Number(activity.end_time) >= Date.now() / 1000 - 90);
        if (current) {
            el('tracking-status').textContent = `Recently active in ${current.process_name}`;
            return;
        }
        el('tracking-status').textContent = 'Activity recorded on this device';
    }

    function appCategory(processName) {
        const name = processName.toLocaleLowerCase();
        if (/chrome|firefox|msedge|browser|opera|brave/.test(name)) return 'Web';
        if (/discord|slack|teams|zalo|telegram|zoom|skype|outlook/.test(name)) return 'Communication';
        if (/steam|game|roblox|valorant|minecraft|epicgames|riotclient/.test(name)) return 'Games';
        if (/spotify|vlc|youtube|netflix|music|media/.test(name)) return 'Media';
        if (/code|devenv|idea|word|excel|powerpnt|notepad|writer|terminal|rider/.test(name)) return 'Productivity';
        return 'Other';
    }

    function renderCategories(apps) {
        const groups = new Map();
        for (const app of apps) {
            const category = appCategory(String(app.process_name || ''));
            groups.set(category, (groups.get(category) || 0) + (Number(app.total_seconds) || 0));
        }
        const values = [...groups.entries()].sort((a, b) => b[1] - a[1]);
        const total = values.reduce((sum, item) => sum + item[1], 0);
        const target = el('category-chart');
        if (!total) {
            target.innerHTML = '<div class="time-empty">No activity to categorize today.</div>';
            return;
        }
        const categoryColors = {
            Web: '#3b6fd4',
            Productivity: '#0b7a75',
            Communication: '#e0a21b',
            Games: '#c8431f',
            Media: '#8a63c9',
            Other: '#7b8794',
        };
        let edge = 0;
        const gradient = values.map(([name, seconds]) => {
            const start = edge;
            edge += seconds / total * 100;
            return `${categoryColors[name]} ${start}% ${edge}%`;
        }).join(', ');
        target.innerHTML = `<div class="time-category-donut" style="background:conic-gradient(${gradient})"><span>${formatDuration(total)}</span></div>
            <div class="time-category-legend">${values.map(([name, seconds]) => `
                <div><i style="background:${categoryColors[name]}"></i><span>${name}</span><b>${Math.round(seconds / total * 100)}%</b></div>`).join('')}</div>`;
    }

    function renderHourly(segments, target) {
        const hours = Array(24).fill(0);
        for (const segment of segments) {
            let cursor = Number(segment.start_time);
            const end = Number(segment.end_time);
            if (!Number.isFinite(cursor) || !Number.isFinite(end) || end <= cursor) continue;
            while (cursor < end) {
                const date = new Date(cursor * 1000);
                const nextHour = new Date(date);
                nextHour.setMinutes(0, 0, 0);
                nextHour.setHours(nextHour.getHours() + 1);
                const boundary = Math.min(end, Math.floor(nextHour.getTime() / 1000));
                if (boundary <= cursor) break;
                hours[date.getHours()] += boundary - cursor;
                cursor = boundary;
            }
        }
        const peak = Math.max(...hours, 1);
        target.innerHTML = hours.map((seconds, hour) => {
            const level = seconds ? Math.max(12, Math.round(seconds / peak * 100)) : 0;
            return `<i class="time-hour-cell${seconds ? ' has-activity' : ''}" style="--hour-level:${level}%;--hour-opacity:${(level / 100).toFixed(2)}" title="${String(hour).padStart(2, '0')}:00 · ${escapeHTML(formatDuration(seconds))}"></i>`;
        }).join('');
    }

    function renderHourlyTotals(secondsByHour, target) {
        const values = Array.from({ length: 24 }, (_, hour) => Number(secondsByHour[hour]) || 0);
        const peak = Math.max(...values, 1);
        target.innerHTML = values.map((seconds, hour) => {
            const height = seconds ? Math.max(3, seconds / peak * 100) : 0;
            return `<div class="time-detail-hour" title="${String(hour).padStart(2, '0')}:00 · ${escapeHTML(formatDuration(seconds))}">
                <i style="height:${height}%"></i><span>${hour % 3 === 0 ? hour : ''}</span>
            </div>`;
        }).join('');
    }

    async function showAppDetails(processName) {
        state.selectedApp = processName;
        const detail = el('application-detail');
        detail.hidden = false;
        detail.innerHTML = `<article class="time-card"><div class="time-empty">Loading details for ${escapeHTML(processName)}…</div></article>`;
        try {
            const data = await requestJSON(`/api/local/app?process_name=${encodeURIComponent(processName)}`);
            if (state.selectedApp !== processName) return;
            const recent = data.recent || [];
            const windows = data.windows || [];
            detail.innerHTML = `<article class="time-card time-app-detail-card">
                <div class="time-section-head">
                    <div><h2>${escapeHTML(data.process_name)}</h2><p>Application activity on this device over the last seven days</p></div>
                    <button type="button" class="time-button" id="close-app-detail">Close</button>
                </div>
                <div class="time-detail-stats">
                    <div><span>Today</span><strong>${escapeHTML(formatDuration(data.today_seconds))}</strong></div>
                    <div><span>Yesterday</span><strong>${escapeHTML(formatDuration(data.yesterday_seconds))}</strong></div>
                    <div><span>This week</span><strong>${escapeHTML(formatDuration(data.week_seconds))}</strong></div>
                    <div><span>Sessions today</span><strong>${Number(data.session_count) || 0}</strong></div>
                    <div><span>Average session</span><strong>${escapeHTML(formatDuration(data.average_session_seconds))}</strong></div>
                    <div><span>Longest session</span><strong>${escapeHTML(formatDuration(data.longest_session_seconds))}</strong></div>
                </div>
                <div class="time-detail-grid">
                    <section><h3>Usage by hour today</h3><div id="app-detail-hours" class="time-detail-hours"></div></section>
                    <section><h3>Daily usage this week</h3><div id="app-detail-days" class="time-week-chart"></div></section>
                </div>
                <section class="time-detail-windows"><h3>Most-used windows today</h3>
                    ${windows.length ? `<div class="time-detail-window-list">${windows.map((window) => `<div><span title="${escapeHTML(window.window_title)}">${escapeHTML(window.window_title)}</span><b>${escapeHTML(formatDuration(window.total_seconds))}</b></div>`).join('')}</div>` : '<div class="time-empty">No window titles recorded today.</div>'}
                </section>
                <section class="time-detail-recent"><h3>Recent sessions</h3>
                    <div class="time-table-wrap"><table class="time-table"><thead><tr><th>Window</th><th>Duration</th><th>Started</th></tr></thead>
                    <tbody>${recent.length ? recent.map((activity) => `<tr><td title="${escapeHTML(activity.window_title)}">${escapeHTML(activity.window_title || '—')}</td><td>${escapeHTML(formatDuration(activity.duration_seconds))}</td><td>${escapeHTML(formatDateTime(activity.start_time))}</td></tr>`).join('') : '<tr><td colspan="3" class="time-empty">No sessions recorded today.</td></tr>'}</tbody></table></div>
                </section>
            </article>`;
            renderHourlyTotals(data.hourly_seconds || [], el('app-detail-hours'));
            renderWeek(el('app-detail-days'), data.daily_totals || [], true);
            el('close-app-detail').addEventListener('click', closeAppDetails);
        } catch (error) {
            if (state.selectedApp === processName) {
                detail.innerHTML = `<article class="time-card"><div class="time-section-head"><div class="time-empty">${escapeHTML(error.message)}</div><button type="button" class="time-button" id="close-app-detail">Close</button></div></article>`;
                el('close-app-detail').addEventListener('click', closeAppDetails);
                showNotice('Application details could not be loaded.');
            }
        }
    }

    function closeAppDetails() {
        state.selectedApp = '';
        el('application-detail').hidden = true;
        el('application-detail').innerHTML = '';
    }

    function updateFocusDisplay(completed = false) {
        const remaining = Math.max(0, Math.ceil((state.focusEndAt - Date.now()) / 1000));
        el('view-overview').querySelector('.time-focus-card').classList.toggle('is-running', Boolean(state.focusEndAt) && !completed);
        if (completed) {
            el('focus-countdown').textContent = '00:00';
            el('focus-toggle').textContent = 'Start focus';
            el('focus-status').textContent = state.breakReminder ? 'Focus complete — take a short break.' : 'Focus session complete.';
            return;
        }
        if (!state.focusEndAt) {
            const minutes = Number(el('focus-duration').value) || 25;
            el('focus-countdown').textContent = `${String(minutes).padStart(2, '0')}:00`;
            el('focus-toggle').textContent = 'Start focus';
            return;
        }
        el('focus-countdown').textContent = `${String(Math.floor(remaining / 60)).padStart(2, '0')}:${String(remaining % 60).padStart(2, '0')}`;
        el('focus-toggle').textContent = 'End focus';
        el('focus-status').textContent = 'Stay focused; this timer runs on this device.';
    }

    function startFocusTimer() {
        const duration = Number(el('focus-duration').value);
        if (![25, 50].includes(duration)) {
            showInline('focus-status', 'Choose a supported focus duration.');
            return;
        }
        state.focusEndAt = Date.now() + duration * 60 * 1000;
        localStorage.setItem('jit-focus-end', String(state.focusEndAt));
        el('focus-status').textContent = '';
        updateFocusDisplay();
    }

    async function loadLimitsPage() {
        showInline('limits-message', '');
        const container = el('limits-list');
        try {
            const data = await requestJSON('/api/parent/children');
            if (!data.logged_in) {
                el('limits-child-select').innerHTML = '<option value="">Sign in to manage family limits</option>';
                container.innerHTML = '<div class="time-empty">Sign in to review limits for an approved family connection. <a href="/login">Sign in</a></div>';
                setLimitsFormDisabled(true);
                return;
            }
            const children = (data.links || []).filter((link) => link.status === 'approved');
            const select = el('limits-child-select');
            select.innerHTML = '<option value="">Select an approved connection</option>' + children.map((link) =>
                `<option value="${escapeHTML(link.other_user_id)}">${escapeHTML(link.other_email)}</option>`).join('');
            if (!children.some((link) => link.other_user_id === state.selectedLimitsChild)) {
                state.selectedLimitsChild = children[0]?.other_user_id || '';
            }
            const selected = children.find((link) => link.other_user_id === state.selectedLimitsChild);
            state.selectedLimitsPermission = selected?.permission_level || 'full';
            select.value = state.selectedLimitsChild;
            if (!selected) {
                container.innerHTML = '<div class="time-empty">No approved family connections are available.</div>';
                el('limits-summary').textContent = '';
                setLimitsFormDisabled(true);
                return;
            }
            setLimitsFormDisabled(state.selectedLimitsPermission === 'view_only');
            el('limits-summary').textContent = state.selectedLimitsPermission === 'view_only'
                ? 'This connection is view-only. You can review limits but cannot change them.'
                : 'Limits are managed for the selected approved family connection.';
            await loadLimitsPageItems();
        } catch (error) {
            container.innerHTML = `<div class="time-empty">${escapeHTML(error.message)}</div>`;
            setLimitsFormDisabled(true);
            showInline('limits-message', 'Application limits could not be loaded.');
        }
    }

    function setLimitsFormDisabled(disabled) {
        el('limits-form').querySelectorAll('input,button').forEach((field) => { field.disabled = disabled; });
        const blocked = el('limits-app-blocked').checked;
        el('limits-app-minutes').required = !blocked;
        el('limits-app-minutes').disabled = disabled || blocked;
    }

    async function loadLimitsPageItems() {
        if (!state.selectedLimitsChild) return;
        try {
            const data = await requestJSON(`/api/parent/limits?child_id=${encodeURIComponent(state.selectedLimitsChild)}`);
            const limits = data.limits || [];
            const canManage = state.selectedLimitsPermission !== 'view_only';
            el('limits-list').innerHTML = limits.length ? limits.map((limit) => `
                <div class="time-family-row">
                    <div><div class="time-device-name">${escapeHTML(limit.process_name)}</div>
                        <div class="time-device-meta">${limit.blocked ? 'Blocked' : limit.daily_limit_sec == null ? 'No time limit' : `${Math.round(limit.daily_limit_sec / 60)} min per day`}</div>
                    </div>
                    ${canManage ? `<button class="time-button" type="button" data-page-delete-limit="${Number(limit.id)}">Remove</button>` : ''}
                </div>`).join('') : '<div class="time-empty">No app limits have been set for this connection.</div>';
            el('limits-list').querySelectorAll('[data-page-delete-limit]').forEach((button) => {
                button.addEventListener('click', () => deletePageLimit(Number(button.dataset.pageDeleteLimit)));
            });
        } catch (error) {
            el('limits-list').innerHTML = `<div class="time-empty">${escapeHTML(error.message)}</div>`;
        }
    }

    async function savePageLimit(event) {
        event.preventDefault();
        if (!state.selectedLimitsChild || state.selectedLimitsPermission === 'view_only') return;
        const processName = el('limits-app-name').value.trim();
        const blocked = el('limits-app-blocked').checked;
        const minutes = Number(el('limits-app-minutes').value);
        if (!processName || (!blocked && (!Number.isInteger(minutes) || minutes < 1 || minutes > 1440))) {
            showInline('limits-message', 'Enter an application name and a daily limit between 1 and 1440 minutes.');
            return;
        }
        try {
            await postFamily('/api/parent/limits/set', {
                child_user_id: state.selectedLimitsChild,
                process_name: processName,
                daily_limit_min: blocked ? null : minutes,
                blocked,
            });
            el('limits-app-name').value = '';
            await loadLimitsPageItems();
            showInline('limits-message', 'Application limit saved.');
        } catch (error) {
            showInline('limits-message', error.message);
        }
    }

    async function deletePageLimit(id) {
        try {
            await postFamily('/api/parent/limits/delete', { id, child_user_id: state.selectedLimitsChild });
            await loadLimitsPageItems();
            showInline('limits-message', 'Application limit removed.');
        } catch (error) {
            showInline('limits-message', error.message);
        }
    }

    async function loadLocalUsage() {
        const data = await requestJSON('/api/local/summary');
        state.usage = data.usage || [];
        return state.usage;
    }

    async function loadRecent() {
        const data = await requestJSON('/api/local/recent');
        state.recent = data.recent || [];
        return state.recent;
    }

    async function loadOverview() {
        showNotice('');
        const results = await Promise.allSettled([
            loadLocalUsage(),
            loadRecent(),
            requestJSON('/api/local/period?period=this_week'),
        ]);
        let failures = 0;
        if (results[0].status === 'fulfilled') {
            const usage = results[0].value;
            const total = usage.reduce((sum, app) => sum + (Number(app.total_seconds) || 0), 0);
            el('today-total').textContent = formatDuration(total);
            const progress = Math.min(100, total / (dailyGoalMin * 60) * 100);
            const remaining = Math.max(0, Math.ceil(dailyGoalMin - total / 60));
            el('goal-progress').style.width = `${progress}%`;
            el('goal-caption').textContent = `${formatDuration(total)} of ${formatDuration(dailyGoalMin * 60)} goal · ${remaining ? `${remaining} min left` : 'Goal reached'}`;
            el('today-app-count').textContent = String(usage.length);
            el('most-used-app').textContent = usage[0]?.process_name || '—';
            el('most-used-duration').textContent = usage[0] ? `${formatDuration(usage[0].total_seconds)} today` : 'No activity recorded yet';
            el('tracking-status').textContent = usage.length ? 'Activity recorded on this device' : 'No activity recorded yet';
            renderAppRows(usage, el('overview-apps'), 5, true);
            renderCategories(usage);
        } else {
            failures++;
            el('overview-apps').innerHTML = '<div class="time-empty">Could not load application usage.</div>';
        }
        if (results[1].status === 'fulfilled') renderRecent(results[1].value, 'overview-recent');
        else {
            failures++;
            el('overview-recent').innerHTML = '<tr><td colspan="4" class="time-empty">Could not load recent activity.</td></tr>';
        }
        if (results[2].status === 'fulfilled') renderWeek(el('week-chart'), results[2].value.daily_totals || [], true);
        else {
            failures++;
            el('week-chart').innerHTML = '<div class="time-empty">Could not load weekly totals.</div>';
        }
        try {
            const timeline = await requestJSON('/api/local/timeline');
            renderHourly(timeline.segments || [], el('hour-heatmap'));
            renderCurrentActivity(results[1].status === 'fulfilled' ? results[1].value : []);
        } catch {
            el('hour-heatmap').innerHTML = '<div class="time-empty">Could not load hourly activity.</div>';
        }
        showNotice(failures ? 'Some activity data could not be loaded. Check the local activity database and try refreshing.' : '');
    }

    async function loadApplications() {
        showNotice('');
        try {
            await loadLocalUsage();
            renderAppRows(filterApplications(), el('applications-list'), Infinity, true);
        } catch (error) {
            el('applications-list').innerHTML = `<div class="time-empty">${escapeHTML(error.message)}</div>`;
            showNotice('Application usage could not be loaded.');
        }
    }

    function filterApplications() {
        const query = el('app-search').value.trim().toLocaleLowerCase();
        return state.usage.filter((app) => String(app.process_name || '').toLocaleLowerCase().includes(query));
    }

    async function loadHistory(period, from, to) {
        showNotice('');
        state.period = period;
        document.querySelectorAll('.time-period-btn').forEach((button) => {
            button.classList.toggle('active', button.dataset.period === period);
        });
        const params = new URLSearchParams({ period });
        if (period === 'custom') {
            params.set('from', from || state.customFrom);
            params.set('to', to || state.customTo || from || state.customFrom);
        }
        const exportParams = new URLSearchParams(params);
        el('history-export').href = `/export/local.csv?${exportParams}`;
        try {
            const data = await requestJSON(`/api/local/period?${params}`);
            const apps = data.usage || [];
            const total = apps.reduce((sum, app) => sum + (Number(app.total_seconds) || 0), 0);
            const periodLabel = data.from === data.to ? data.from : `${data.from} – ${data.to}`;
            el('history-summary').textContent = `${formatDuration(total)} tracked across ${apps.length} applications · ${periodLabel}`;
            renderAppRows(apps, el('history-rows'), Infinity);
            if (Array.isArray(data.daily_totals)) {
                renderWeek(el('history-chart'), data.daily_totals, period === 'this_week');
                renderHistoryStats(data.daily_totals, apps.length, total);
            }
            else {
                const day = await requestJSON(`/api/local/timeline?date=${encodeURIComponent(data.date)}`);
                renderTimeline(day, el('history-chart'));
                renderHistoryStats([{ total_seconds: total }], apps.length, total);
            }
        } catch (error) {
            el('history-summary').textContent = 'History could not be loaded.';
            el('history-rows').innerHTML = `<div class="time-empty">${escapeHTML(error.message)}</div>`;
            el('history-chart').innerHTML = '';
            showNotice('Could not load activity history. Please try another period.');
        }
    }

    async function loadCloud() {
        state.cloudRange = el('cloud-range').value || 'week';
        const range = encodeURIComponent(state.cloudRange);
        el('cloud-export').href = `/export/csv?range=${range}`;
        el('cloud-report').href = `/report?range=${range}`;
        showInline('cloud-message', '');
        el('cloud-apps').innerHTML = '<div class="time-empty">Loading synced usage…</div>';
        el('cloud-days').innerHTML = '<div class="time-empty">Loading daily usage…</div>';
        el('cloud-recent').innerHTML = '<tr><td colspan="5" class="time-empty">Loading cloud activity…</td></tr>';

        const results = await Promise.allSettled([
            requestJSON(`/api/cloud/summary?range=${range}`),
            requestJSON(`/api/cloud/daily?range=${range}`),
            requestJSON('/api/cloud/recent'),
        ]);
        if (results.every((result) => result.status === 'rejected')) {
            const error = results[0].reason;
            showInline('cloud-message', `${error.message} Sign in to view synced account activity.`);
            el('cloud-apps').innerHTML = '<div class="time-empty"><a href="/login">Sign in</a> to load synced activity.</div>';
            el('cloud-days').innerHTML = '';
            el('cloud-recent').innerHTML = '<tr><td colspan="5" class="time-empty">Sign in to load synced activity.</td></tr>';
            el('cloud-summary').textContent = '';
            return;
        }

        const summary = results[0].status === 'fulfilled' ? results[0].value : null;
        const daily = results[1].status === 'fulfilled' ? results[1].value : null;
        const recent = results[2].status === 'fulfilled' ? results[2].value.records || [] : [];

        if (summary) {
            const apps = summary.apps || [];
            renderAppRows(apps, el('cloud-apps'));
            const total = apps.reduce((sum, app) => sum + (Number(app.total_seconds) || 0), 0);
            el('cloud-summary').textContent = `${formatDuration(total)} across ${apps.length} applications · ${state.cloudRange === 'today' ? 'Today' : state.cloudRange === 'month' ? 'Last 30 days' : 'Last 7 days'}`;
        } else {
            el('cloud-apps').innerHTML = '<div class="time-empty">Synced app usage could not be loaded.</div>';
        }

        if (daily) {
            const dailyTotals = (daily.days || []).map((date, index) => ({
                date,
                total_seconds: Number(daily.totals?.[index]) || 0,
            }));
            renderWeek(el('cloud-days'), dailyTotals, true);
        } else {
            el('cloud-days').innerHTML = '<div class="time-empty">Daily synced usage could not be loaded.</div>';
        }

        const rangeStart = state.cloudRange === 'today'
            ? new Date(new Date().setHours(0, 0, 0, 0)).getTime() / 1000
            : new Date(new Date().setHours(0, 0, 0, 0)).getTime() / 1000 - (state.cloudRange === 'month' ? 29 : 6) * 86400;
        const inRange = recent.filter((activity) => Number(activity.start_time) >= rangeStart);
        if (inRange.length) {
            el('cloud-recent').innerHTML = inRange.map((activity) => `<tr>
                <td>${escapeHTML(safeDeviceLabel({ hostname: activity.device_id, device_id: activity.device_id }) || 'Device')}</td>
                <td>${escapeHTML(activity.process_name || 'Unknown application')}</td>
                <td class="time-cell-muted" title="${escapeHTML(activity.window_title || '')}">${escapeHTML(activity.window_title || '—')}</td>
                <td>${escapeHTML(formatDuration(activity.duration_seconds))}</td>
                <td class="time-cell-muted">${escapeHTML(formatDateTime(activity.start_time))}</td>
            </tr>`).join('');
        } else {
            el('cloud-recent').innerHTML = '<tr><td colspan="5" class="time-empty">No synced activity found in this period.</td></tr>';
        }
        if (results.some((result) => result.status === 'rejected')) {
            showInline('cloud-message', 'Some synced activity could not be loaded. Try refreshing or sign in again.');
        }
    }

    function renderHistoryStats(days, appCount, totalSeconds) {
        const validDays = days.length;
        const peak = days.reduce((best, day) => Math.max(best, Number(day.total_seconds) || 0), 0);
        const average = validDays ? totalSeconds / validDays : 0;
        el('history-day-breakdown').innerHTML = `
            <div><span>Daily average</span><strong>${escapeHTML(formatDuration(average))}</strong></div>
            <div><span>Most active day</span><strong>${escapeHTML(formatDuration(peak))}</strong></div>
            <div><span>Applications</span><strong>${appCount}</strong></div>`;
    }

    function renderTimeline(day, target) {
        const segments = day.segments || [];
        if (!segments.length) {
            target.innerHTML = '<div class="time-empty">No activity recorded for this day.</div>';
            return;
        }
        const bars = segments.map((segment) => {
            const start = Math.max(0, Math.min(100, Number(segment.start_pct) || 0));
            const width = Math.max(0.15, Math.min(100 - start, Number(segment.width_pct) || 0));
            const color = /^#[\da-f]{6}$/i.test(segment.color || '') ? segment.color : '#0b7a75';
            const title = `${segment.process_name} · ${formatDateTime(segment.start_time)} – ${formatDateTime(segment.end_time)}`;
            return `<span class="time-timeline-segment" title="${escapeHTML(title)}" style="left:${start}%;width:${width}%;background:${color}"></span>`;
        }).join('');
        const ticks = ['00:00', '06:00', '12:00', '18:00', '24:00'];
        const legend = (day.legend || []).slice(0, 8).map((entry) => {
            const color = /^#[\da-f]{6}$/i.test(entry.color || '') ? entry.color : '#0b7a75';
            return `<span class="time-timeline-legend-item"><i style="background:${color}"></i>${escapeHTML(entry.process_name)} <b>${escapeHTML(formatDuration(entry.total_seconds))}</b></span>`;
        }).join('');
        target.innerHTML = `<div class="time-timeline-wrap">
            <div class="time-timeline">${bars}</div>
            <div class="time-time-ruler">${ticks.map((tick) => `<span>${tick}</span>`).join('')}</div>
            <div class="time-timeline-legend">${legend}</div>
        </div>`;
    }

    function safeDeviceLabel(machine) {
        const label = String(machine.hostname || '');
        if (/^PC-[0-9A-Fa-f]{8}$/.test(label)) return label;
        const deviceID = String(machine.device_id || '');
        return /^PC-[0-9A-Fa-f]{8}$/.test(deviceID) ? `Device ${deviceID.slice(-4)}` : '';
    }

    async function loadDevices() {
        showInline('devices-message', '');
        try {
            const [data, inboxResult] = await Promise.all([
                requestJSON('/api/machines'),
                requestJSON('/api/inbox').catch((error) => ({ error })),
            ]);
            const container = el('devices-list');
            if (!data.logged_in) {
                container.innerHTML = '<div class="time-empty">Sign in to see devices linked to your account. <a href="/login">Sign in</a></div>';
                el('device-inbox').innerHTML = '<div class="time-empty">Sign in to load messages. <a href="/login">Sign in</a></div>';
                return;
            }
            if (inboxResult.error) {
                el('device-inbox').innerHTML = `<div class="time-empty">${escapeHTML(inboxResult.error.message)}</div>`;
            } else {
                renderInbox(inboxResult.messages || []);
            }
            const machines = data.machines || [];
            if (!machines.length) {
                container.innerHTML = '<div class="time-empty">No other devices are connected to this account yet.</div>';
                return;
            }
            container.innerHTML = machines.map((machine) => {
                const label = safeDeviceLabel(machine);
                const isSelf = machine.device_id === data.self_device_id;
                const seen = Date.parse(machine.last_seen);
                const online = Number.isFinite(seen) && Date.now() - seen < 90000;
                const staleLabel = label ? escapeHTML(label) : 'Unverified device';
                return `<div class="time-device-row">
                    <div><div class="time-device-name">${staleLabel}${isSelf ? ' <span class="time-device-meta">(this device)</span>' : ''}</div>
                        <div class="time-device-meta">${online ? 'Last active just now' : 'Last seen ' + escapeHTML(machine.last_seen ? new Date(machine.last_seen).toLocaleString() : 'unknown')}</div>
                    </div>
                    <div class="time-family-actions">
                        <span class="time-status-pill${online ? '' : ' is-offline'}">${online ? 'Online' : 'Offline'}</span>
                        ${!isSelf && label ? `<button type="button" class="time-button" data-device="${escapeHTML(machine.device_id)}">Message</button>` : ''}
                        ${!label ? `<button type="button" class="time-button" data-remove-device="${escapeHTML(machine.device_id)}">Remove legacy record</button>` : ''}
                    </div>
                </div>`;
            }).join('');
            container.querySelectorAll('[data-device]').forEach((button) => {
                button.addEventListener('click', () => openDeviceThread(button.dataset.device, button.textContent));
            });
            container.querySelectorAll('[data-remove-device]').forEach((button) => {
                button.addEventListener('click', () => removeLegacyDevice(button.dataset.removeDevice));
            });
        } catch (error) {
            el('devices-list').innerHTML = `<div class="time-empty">${escapeHTML(error.message)}</div>`;
            showInline('devices-message', 'Device information could not be loaded.');
        }
    }

    function renderInbox(messages) {
        const container = el('device-inbox');
        if (!messages.length) {
            container.innerHTML = '<div class="time-empty">No messages have been sent to this device.</div>';
            return;
        }
        container.innerHTML = messages.map((message) => {
            const sender = /^PC-[0-9A-Fa-f]{8}$/.test(message.sender_device_id || '')
                ? `Device ${(message.sender_device_id || '').slice(-4)}` : 'Connected device';
            const body = message.kind === 'message' ? message.payload : 'A device shared data with you.';
            const unread = !message.read_at;
            return `<div class="time-inbox-row${unread ? ' unread' : ''}">
                <div><strong>${escapeHTML(sender)}${unread ? ' · New' : ''}</strong>
                    <p>${escapeHTML(body)}</p><span>${escapeHTML(message.created_at || '')}</span>
                </div>
                ${unread ? `<button type="button" class="time-button" data-mark-read="${Number(message.id)}">Mark read</button>` : ''}
            </div>`;
        }).join('');
        container.querySelectorAll('[data-mark-read]').forEach((button) => {
            button.addEventListener('click', () => markInboxRead(Number(button.dataset.markRead)));
        });
    }

    async function markInboxRead(id) {
        try {
            await requestJSON('/api/inbox/read', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ id }),
            });
            await loadDevices();
        } catch (error) {
            showInline('inbox-message', error.message);
        }
    }

    async function removeLegacyDevice(deviceID) {
        if (!confirm('Remove this unverified legacy device record?')) return;
        try {
            await requestJSON('/api/machines/remove', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ device_id: deviceID }),
            });
            await loadDevices();
        } catch (error) {
            showInline('devices-message', error.message);
        }
    }

    async function openDeviceThread(deviceID, label) {
        state.deviceID = deviceID;
        el('device-chat').hidden = false;
        el('device-chat-title').textContent = `Messages · ${label}`;
        await loadDeviceThread();
        el('device-chat').scrollIntoView({ behavior: 'smooth', block: 'nearest' });
    }

    async function loadDeviceThread() {
        if (!state.deviceID) return;
        try {
            const data = await requestJSON(`/api/thread?device_id=${encodeURIComponent(state.deviceID)}`);
            const messages = data.messages || [];
            const container = el('device-messages');
            container.innerHTML = messages.length ? messages.map((message) => `
                <div class="time-message${message.sender_device_id === data.self_device_id ? ' mine' : ''}">
                    ${escapeHTML(message.kind === 'message' ? message.payload : 'Shared device data')}
                    <span class="time-message-meta">${escapeHTML(message.created_at || '')}</span>
                </div>`).join('') : '<div class="time-empty">No messages in this conversation yet.</div>';
            container.scrollTop = container.scrollHeight;
        } catch (error) {
            showInline('devices-message', error.message);
        }
    }

    async function loadFamily() {
        showInline('family-message', '');
        const results = await Promise.allSettled([
            requestJSON('/api/parent/children'),
            requestJSON('/api/parent/parents'),
        ]);
        const childResult = results[0];
        const parentResult = results[1];
        if (childResult.status === 'fulfilled' && !childResult.value.logged_in) {
            el('family-children').innerHTML = '<div class="time-empty">Sign in to manage family connections. <a href="/login">Sign in</a></div>';
            el('family-parents').innerHTML = '<div class="time-empty">—</div>';
            return;
        }
        if (childResult.status === 'rejected') {
            el('family-children').innerHTML = `<div class="time-empty">${escapeHTML(childResult.reason.message)}</div>`;
        } else {
            state.children = childResult.value.links || [];
            renderChildren();
        }
        if (parentResult.status === 'rejected') {
            el('family-parents').innerHTML = `<div class="time-empty">${escapeHTML(parentResult.reason.message)}</div>`;
        } else {
            renderParents(parentResult.value.links || []);
        }
    }

    function statusText(status) {
        return status === 'approved' ? 'Connected' : status === 'pending' ? 'Pending' : 'Revoked';
    }

    function renderChildren() {
        const container = el('family-children');
        const activeChildren = state.children.filter((link) => link.status === 'approved');
        const selected = activeChildren.find((link) => link.other_user_id === state.selectedChild);
        if (!selected && activeChildren.length) {
            state.selectedChild = activeChildren[0].other_user_id;
            state.selectedPermission = activeChildren[0].permission_level || 'full';
        }
        if (!activeChildren.some((link) => link.other_user_id === state.selectedChild)) {
            state.selectedChild = '';
            state.selectedPermission = 'full';
        }
        container.innerHTML = state.children.length ? state.children.map((link) => {
            const linkID = Number(link.id);
            return `<div class="time-family-row">
                <div><div class="time-device-name">${escapeHTML(link.other_email)}</div><div class="time-device-meta">${escapeHTML(statusText(link.status))}</div></div>
                <div class="time-family-actions">
                    ${link.status === 'approved' ? `<select aria-label="Permission for ${escapeHTML(link.other_email)}" data-permission-id="${linkID}">
                        <option value="full"${link.permission_level !== 'view_only' ? ' selected' : ''}>Can manage</option>
                        <option value="view_only"${link.permission_level === 'view_only' ? ' selected' : ''}>View only</option>
                    </select><button class="time-button" type="button" data-select-child="${escapeHTML(link.other_user_id)}">Manage limits</button>` : ''}
                    ${link.status !== 'revoked' ? `<button class="time-button" type="button" data-revoke-link="${linkID}">Revoke</button>` : ''}
                </div>
            </div>`;
        }).join('') : '<div class="time-empty">No family members invited yet.</div>';
        container.querySelectorAll('[data-permission-id]').forEach((select) => {
            select.addEventListener('change', () => updatePermission(Number(select.dataset.permissionId), select.value));
        });
        container.querySelectorAll('[data-select-child]').forEach((button) => {
            button.addEventListener('click', () => {
                const selectedLink = state.children.find((link) => link.other_user_id === button.dataset.selectChild);
                state.selectedChild = button.dataset.selectChild;
                state.selectedPermission = selectedLink?.permission_level || 'full';
                renderChildren();
                el('family-child-select').value = state.selectedChild;
            });
        });
        container.querySelectorAll('[data-revoke-link]').forEach((button) => {
            button.addEventListener('click', () => revokeFamilyLink(Number(button.dataset.revokeLink)));
        });
        renderChildSelect();
        loadFamilyMonitor();
    }

    function renderChildSelect() {
        const select = el('family-child-select');
        const approved = state.children.filter((link) => link.status === 'approved');
        select.innerHTML = '<option value="">Select an approved connection</option>' + approved.map((link) =>
            `<option value="${escapeHTML(link.other_user_id)}">${escapeHTML(link.other_email)}</option>`).join('');
        select.value = state.selectedChild;
        if (state.selectedChild) loadLimits();
        else {
            el('family-limits').innerHTML = '<div class="time-empty">Choose an approved connection to view limits.</div>';
            el('family-limit-form').querySelectorAll('input,button').forEach((field) => { field.disabled = true; });
        }
    }

    function renderParents(links) {
        const container = el('family-parents');
        container.innerHTML = links.length ? links.map((link) => `
            <div class="time-family-row">
                <div><div class="time-device-name">${escapeHTML(link.other_email)}</div><div class="time-device-meta">${escapeHTML(statusText(link.status))}</div></div>
                <div class="time-family-actions">
                    ${link.status === 'pending' ? `<button type="button" class="time-button time-button-primary" data-approve-link="${Number(link.id)}">Approve</button>` : ''}
                    ${link.status !== 'revoked' ? `<button type="button" class="time-button" data-revoke-link="${Number(link.id)}">Revoke</button>` : ''}
                </div>
            </div>`).join('') : '<div class="time-empty">No invitations to you.</div>';
        container.querySelectorAll('[data-approve-link]').forEach((button) => {
            button.addEventListener('click', () => approveFamilyLink(Number(button.dataset.approveLink)));
        });
        container.querySelectorAll('[data-revoke-link]').forEach((button) => {
            button.addEventListener('click', () => revokeFamilyLink(Number(button.dataset.revokeLink)));
        });
    }

    async function loadLimits() {
        const childID = state.selectedChild;
        const form = el('family-limit-form');
        const readonly = state.selectedPermission === 'view_only';
        form.querySelectorAll('input,button').forEach((field) => { field.disabled = !childID || readonly; });
        showInline('family-limits-note', readonly ? 'This connection is view-only; you cannot change app limits.' : '');
        if (!childID) return;
        try {
            const data = await requestJSON(`/api/parent/limits?child_id=${encodeURIComponent(childID)}`);
            const limits = data.limits || [];
            el('family-limits').innerHTML = limits.length ? limits.map((limit) => `
                <div class="time-family-row">
                    <div><div class="time-device-name">${escapeHTML(limit.process_name)}</div>
                        <div class="time-device-meta">${limit.blocked ? 'Blocked' : limit.daily_limit_sec == null ? 'No time limit' : `${Math.round(limit.daily_limit_sec / 60)} min per day`}</div>
                    </div>
                    ${readonly ? '' : `<button class="time-button" type="button" data-delete-limit="${Number(limit.id)}">Remove</button>`}
                </div>`).join('') : '<div class="time-empty">No app limits have been set.</div>';
            el('family-limits').querySelectorAll('[data-delete-limit]').forEach((button) => {
                button.addEventListener('click', () => deleteLimit(Number(button.dataset.deleteLimit)));
            });
        } catch (error) {
            el('family-limits').innerHTML = `<div class="time-empty">${escapeHTML(error.message)}</div>`;
        }
    }

    async function loadFamilyMonitor() {
        const childID = state.selectedChild;
        const status = el('family-monitor-status');
        if (!childID) {
            status.innerHTML = '<div class="time-empty">Select an approved family member to view activity.</div>';
            el('family-monitor-apps').innerHTML = '<div class="time-empty">No member selected.</div>';
            el('family-monitor-recent').innerHTML = '<tr><td colspan="4" class="time-empty">No member selected.</td></tr>';
            showInline('family-monitor-message', '');
            return;
        }

        showInline('family-monitor-message', '');
        status.innerHTML = '<div class="time-empty">Loading family member activity…</div>';
        const results = await Promise.allSettled([
            requestJSON(`/api/parent/child-summary?child_id=${encodeURIComponent(childID)}`),
            requestJSON(`/api/parent/child-heartbeat?child_id=${encodeURIComponent(childID)}`),
        ]);
        if (state.selectedChild !== childID) return;

        const summary = results[0].status === 'fulfilled' ? results[0].value : null;
        const heartbeat = results[1].status === 'fulfilled' ? results[1].value : null;
        if (!summary && !heartbeat) {
            const error = results[0].reason;
            status.innerHTML = `<div class="time-empty">${escapeHTML(error.message)} · <a href="/login">Sign in</a> and check the family connection.</div>`;
            el('family-monitor-apps').innerHTML = '<div class="time-empty">Activity could not be loaded.</div>';
            el('family-monitor-recent').innerHTML = '<tr><td colspan="4" class="time-empty">Activity could not be loaded.</td></tr>';
            return;
        }

        const machines = heartbeat?.machines || [];
        status.innerHTML = machines.length ? machines.map((machine) => {
            const label = safeDeviceLabel(machine) || 'Connected device';
            const seen = Date.parse(machine.last_seen || '');
            const online = Number.isFinite(seen) && Date.now() - seen < 90_000;
            const lastSeen = Number.isFinite(seen) ? new Date(seen).toLocaleString() : 'unknown';
            return `<div class="time-device-row">
                <div><div class="time-device-name">${escapeHTML(label)}</div><div class="time-device-meta">Last seen ${escapeHTML(lastSeen)}</div></div>
                <span class="time-status-pill${online ? '' : ' is-offline'}">${online ? 'Online' : 'Offline'}</span>
            </div>`;
        }).join('') : '<div class="time-empty">No devices have reported in for this family member.</div>';

        if (summary) {
            renderAppRows(summary.apps || [], el('family-monitor-apps'));
            const recent = summary.recent || [];
            el('family-monitor-recent').innerHTML = recent.length ? recent.map((activity) => `<tr>
                <td>${escapeHTML(activity.process_name || 'Unknown application')}</td>
                <td class="time-cell-muted" title="${escapeHTML(activity.window_title || '')}">${escapeHTML(activity.window_title || '—')}</td>
                <td>${escapeHTML(formatDuration(activity.duration_seconds))}</td>
                <td class="time-cell-muted">${escapeHTML(formatDateTime(activity.start_time))}</td>
            </tr>`).join('') : '<tr><td colspan="4" class="time-empty">No recent activity has synced today.</td></tr>';
        } else {
            el('family-monitor-apps').innerHTML = '<div class="time-empty">App usage could not be loaded.</div>';
            el('family-monitor-recent').innerHTML = '<tr><td colspan="4" class="time-empty">Recent activity could not be loaded.</td></tr>';
        }
        if (results.some((result) => result.status === 'rejected')) {
            showInline('family-monitor-message', 'Some family activity information could not be loaded.');
        }
    }

    async function generateInsights() {
        const button = el('insights-generate');
        button.disabled = true;
        el('insights-result').hidden = true;
        showInline('insights-message', 'Generating insights…');
        try {
            const source = el('insights-source').value;
            const data = await requestJSON(`/api/local/insights?source=${encodeURIComponent(source)}`);
            el('insights-summary').textContent = data.summary || '';
            const apps = data.apps || [];
            el('insights-apps').innerHTML = apps.length ? apps.map((app) => `<tr>
                <td>${escapeHTML(app.process_name)}</td><td>${escapeHTML(app.category)}</td><td>${escapeHTML(app.note)}</td>
            </tr>`).join('') : '<tr><td colspan="3" class="time-empty">No application insights were returned.</td></tr>';
            const recommendations = data.recommendations || [];
            el('insights-recommendations').innerHTML = recommendations.length
                ? recommendations.map((recommendation) => `<li>${escapeHTML(recommendation)}</li>`).join('')
                : '<li>No suggestions available.</li>';
            el('insights-result').hidden = false;
            showInline('insights-message', '');
        } catch (error) {
            showInline('insights-message', error.message);
        } finally {
            button.disabled = false;
        }
    }

    function renderChatMessages() {
        const container = el('chat-messages');
        container.innerHTML = state.chatHistory.map((message) =>
            `<div class="time-ai-message ${message.role === 'user' ? 'user' : 'model'}">${escapeHTML(message.text)}</div>`,
        ).join('');
        container.scrollTop = container.scrollHeight;
    }

    async function sendUsageChat(event) {
        event.preventDefault();
        const input = el('usage-chat-input');
        const text = input.value.trim();
        if (!text) return;

        state.chatHistory.push({ role: 'user', text });
        const pending = { role: 'model', text: 'Thinking…' };
        state.chatHistory.push(pending);
        renderChatMessages();
        input.value = '';
        input.disabled = true;
        el('usage-chat-send').disabled = true;
        showInline('chat-message', '');
        try {
            const history = state.chatHistory.filter((message) => message !== pending);
            const data = await requestJSON('/api/local/chat', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ history }),
            });
            pending.text = data.reply || 'No response was returned.';
            if (state.chatHistory.length > 20) state.chatHistory.splice(0, state.chatHistory.length - 20);
        } catch (error) {
            state.chatHistory.splice(state.chatHistory.indexOf(pending), 1);
            showInline('chat-message', error.message);
        } finally {
            renderChatMessages();
            input.disabled = false;
            el('usage-chat-send').disabled = false;
            input.focus();
        }
    }

    async function postFamily(path, body) {
        await requestJSON(path, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(body),
        });
    }

    async function updatePermission(id, permission) {
        try {
            await postFamily('/api/parent/permission', { id, permission_level: permission });
            const link = state.children.find((entry) => entry.id === id);
            if (link) link.permission_level = permission;
            if (link?.other_user_id === state.selectedChild) {
                state.selectedPermission = permission;
                loadLimits();
            }
            showInline('family-message', 'Permission updated.');
        } catch (error) {
            showInline('family-message', error.message);
            loadFamily();
        }
    }

    async function revokeFamilyLink(id) {
        try {
            await postFamily('/api/parent/revoke', { id });
            await loadFamily();
        } catch (error) {
            showInline('family-message', error.message);
        }
    }

    async function approveFamilyLink(id) {
        try {
            await postFamily('/api/parent/approve', { id });
            await loadFamily();
        } catch (error) {
            showInline('family-message', error.message);
        }
    }

    async function deleteLimit(id) {
        try {
            await postFamily('/api/parent/limits/delete', { id, child_user_id: state.selectedChild });
            await loadLimits();
        } catch (error) {
            showInline('family-limits-note', error.message);
        }
    }

    document.querySelectorAll('.time-nav-link[data-view], .time-text-link[href*="?view="]').forEach((link) => {
        link.addEventListener('click', (event) => {
            event.preventDefault();
            setView(new URL(link.href).searchParams.get('view'));
        });
    });
    document.querySelectorAll('.time-period-btn').forEach((button) => {
        button.addEventListener('click', () => loadHistory(button.dataset.period));
    });
    el('app-search').addEventListener('input', () => renderAppRows(filterApplications(), el('applications-list'), Infinity, true));
    for (const container of [el('overview-apps'), el('applications-list')]) {
        container.addEventListener('click', (event) => {
            const button = event.target.closest('[data-process-name]');
            if (!button) return;
            const processName = button.dataset.processName;
            if (state.view !== 'apps') setView('apps');
            showAppDetails(processName);
        });
    }
    el('edit-goal').addEventListener('click', () => { window.location.href = '/settings#screen-time-goal'; });
    el('theme-toggle').addEventListener('click', () => {
        const theme = currentTheme() === 'dark' ? 'light' : 'dark';
        root.dataset.theme = theme;
        localStorage.setItem('jit-theme', theme);
        el('theme-toggle').innerHTML = themeIcon(theme);
        el('theme-toggle').setAttribute('aria-label', `Switch to ${theme === 'dark' ? 'light' : 'dark'} theme`);
    });
    const savedTheme = localStorage.getItem('jit-theme');
    if (savedTheme === 'light' || savedTheme === 'dark') root.dataset.theme = savedTheme;
    el('theme-toggle').innerHTML = themeIcon(currentTheme());
    el('theme-toggle').setAttribute('aria-label', `Switch to ${currentTheme() === 'dark' ? 'light' : 'dark'} theme`);
    el('focus-break-reminder').checked = state.breakReminder;
    el('focus-break-reminder').addEventListener('change', () => {
        state.breakReminder = el('focus-break-reminder').checked;
        localStorage.setItem('jit-break-reminder', state.breakReminder ? 'on' : 'off');
    });
    el('focus-duration').addEventListener('change', () => {
        if (!state.focusEndAt) updateFocusDisplay();
    });
    el('focus-toggle').addEventListener('click', () => {
        if (state.focusEndAt) {
            state.focusEndAt = 0;
            localStorage.removeItem('jit-focus-end');
            el('focus-status').textContent = 'Focus session ended.';
            updateFocusDisplay();
        } else {
            startFocusTimer();
        }
    });
    el('limits-child-select').addEventListener('change', async () => {
        state.selectedLimitsChild = el('limits-child-select').value;
        await loadLimitsPage();
    });
    el('limits-app-blocked').addEventListener('change', () => {
        el('limits-app-minutes').required = !el('limits-app-blocked').checked;
        el('limits-app-minutes').disabled = el('limits-app-blocked').checked || state.selectedLimitsPermission === 'view_only';
    });
    el('limits-form').addEventListener('submit', savePageLimit);
    el('devices-refresh').addEventListener('click', loadDevices);
    el('inbox-refresh').addEventListener('click', loadDevices);
    el('family-monitor-refresh').addEventListener('click', loadFamilyMonitor);
    el('cloud-range').addEventListener('change', loadCloud);
    el('insights-generate').addEventListener('click', generateInsights);
    el('usage-chat-form').addEventListener('submit', sendUsageChat);
    el('history-print').addEventListener('click', () => {
        document.body.classList.add('time-print-history');
        const cleanup = () => document.body.classList.remove('time-print-history');
        window.addEventListener('afterprint', cleanup, { once: true });
        window.print();
        window.setTimeout(cleanup, 1000);
    });
    el('family-child-select').addEventListener('change', () => {
        state.selectedChild = el('family-child-select').value;
        const link = state.children.find((entry) => entry.other_user_id === state.selectedChild);
        state.selectedPermission = link?.permission_level || 'full';
        loadLimits();
    });
    el('history-apply').addEventListener('click', () => {
        const from = el('history-from').value;
        const to = el('history-to').value || from;
        if (!from || (to && to < from)) {
            showNotice('Choose a valid date range.');
            return;
        }
        state.customFrom = from;
        state.customTo = to;
        loadHistory('custom', from, to);
    });
    el('family-invite-form').addEventListener('submit', async (event) => {
        event.preventDefault();
        const email = el('family-invite-email').value.trim();
        try {
            await postFamily('/api/parent/invite', { child_email: email });
            el('family-invite-email').value = '';
            await loadFamily();
            showInline('family-message', 'Invitation sent.');
        } catch (error) {
            showInline('family-message', error.message);
        }
    });
    el('family-limit-form').addEventListener('submit', async (event) => {
        event.preventDefault();
        if (!state.selectedChild || state.selectedPermission === 'view_only') return;
        const processName = el('family-limit-app').value.trim();
        if (!processName) return;
        const blocked = el('family-limit-blocked').checked;
        const minutes = Math.min(1440, Math.max(1, Number(el('family-limit-minutes').value) || 60));
        try {
            await postFamily('/api/parent/limits/set', {
                child_user_id: state.selectedChild,
                process_name: processName,
                daily_limit_min: blocked ? null : minutes,
                blocked,
            });
            el('family-limit-app').value = '';
            await loadLimits();
            showInline('family-limits-note', 'App limit saved.');
        } catch (error) {
            showInline('family-limits-note', error.message);
        }
    });
    el('device-message-form').addEventListener('submit', async (event) => {
        event.preventDefault();
        if (!state.deviceID) return;
        const input = el('device-message-input');
        const payload = input.value.trim();
        if (!payload) return;
        try {
            await requestJSON('/api/send', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ target_device_id: state.deviceID, kind: 'message', payload }),
            });
            input.value = '';
            await loadDeviceThread();
        } catch (error) {
            showInline('devices-message', error.message);
        }
    });

    const todayLabel = new Intl.DateTimeFormat(undefined, { weekday: 'long', month: 'long', day: 'numeric' });
    el('today-label').textContent = todayLabel.format(new Date());
    if (state.focusEndAt && state.focusEndAt <= Date.now()) {
        state.focusEndAt = 0;
        localStorage.removeItem('jit-focus-end');
        updateFocusDisplay(true);
    } else {
        updateFocusDisplay();
    }
    window.setInterval(() => {
        if (!state.focusEndAt) return;
        if (state.focusEndAt <= Date.now()) {
            state.focusEndAt = 0;
            localStorage.removeItem('jit-focus-end');
            updateFocusDisplay(true);
            return;
        }
        updateFocusDisplay();
    }, 1000);
    const initialView = new URLSearchParams(location.search).get('view') || 'overview';
    setView(initialView, false);
})();
