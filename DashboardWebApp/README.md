# JustInTime Dashboard (Go rewrite)

This is a Go rewrite of the previous Flask dashboard (`dashboard/app.pyw`
and friends). It keeps the same look, the same routes/JSON shapes (so
`static/js/dashboard.js`'s Chart.js wiring works with only additive
changes), the same `%APPDATA%\JustInTime\dashboard_settings.json` file
(your font/language/threshold prefs carry over automatically), and the
same Supabase project — and adds four things the Python version didn't
have:

1. **A local, fully-offline view.** A new "Local Activity" tab reads
   `justintime.db` (the same SQLite file the C agent writes) directly,
   with no network and no login required. Every other route in the old
   dashboard required login for even system stats; this rewrite only
   requires login for the Cloud/Devices tabs.
2. **A tray icon** with an "Open Dashboard" button, plus a "Launch at
   login" toggle that registers the dashboard to start automatically
   every time you log into Windows (via the registry Run key, the same
   mechanism `settings.c` uses for the agent itself, but a separate
   entry so the two never interfere).
3. **Device-to-device messaging by `device_id`.** A new "Devices" tab
   lets you send a short message or a "data" payload (e.g. today's
   summary) to another of your own machines. Like activity sync, this
   is always relayed through Supabase — the dashboard never opens a
   direct connection to another computer.
4. A single static binary: no `pip install`, no Python runtime on the
   target machine. Templates, CSS and JS are embedded in the `.exe`.

## Project layout

```
dashboard-go/
  cmd/dashboard/main.go        entry point (HTTP server + tray)
  internal/
    config/        paths shared with the C agent (%APPDATA%\JustInTime)
    localdb/        read-only access to justintime.db (offline source)
    cloud/          Supabase auth + REST client + device messaging
    dashsession/    the dashboard's own persisted login (DPAPI on Windows)
    dashsettings/   port of dashboard_settings.py (same JSON file)
    i18n/           port of i18n.py (en/vi), + new keys for Local/Devices
    sysstats/       port of system_stats.py using gopsutil
    server/         HTTP routes + embedded templates/static (web/)
    tray/           system tray icon (Windows-only)
    autostart/      registry Run-key toggle (Windows-only)
  migrations/002_device_messages.sql   run once in the Supabase SQL editor
```

## Building

Requires Go 1.22+ and internet access on the *build* machine (to fetch
`modernc.org/sqlite`, `github.com/getlantern/systray`,
`github.com/shirou/gopsutil/v3`, `golang.org/x/sys`) — the resulting
`.exe` needs no runtime dependencies at all.

```bash
cd dashboard-go
go mod tidy
GOOS=windows GOARCH=amd64 go build -o dashboard.exe ./cmd/dashboard
```

Run it locally for development (works on Linux/macOS too, minus the
tray icon and DPAPI-backed session — see the `_other.go` files):

```bash
go run ./cmd/dashboard
```

Then open http://127.0.0.1:5000 (same port the Python version used).

## Setting up device messaging

Run `migrations/002_device_messages.sql` once in your Supabase
project's SQL editor (Dashboard > SQL Editor). It adds a
`device_messages` table with Row Level Security so a user can only ever
read/write messages tied to their own account — the same protection
`activity_logs` already has.

## Wiring up the tray + autostart

- Run `dashboard.exe --tray` to show the tray icon.
- Click **"Launch at login"** in the tray menu to register the
  dashboard to start automatically the next time you log into Windows
  (writes `HKCU\...\Run\JustInTimeDashboard`, pointing at
  `dashboard.exe --tray`).
- Click **"Open Dashboard"** any time to open it in your browser.

## Notes / known simplifications

- **Network interface link speed** (`speed_mbps`) isn't populated —
  gopsutil has no portable API for it (psutil's `net_if_stats().speed`
  relies on OS-specific calls). The UI already falls back to "N/A" for
  a falsy value, so this degrades gracefully.
- **Chart.js** is still loaded from a CDN (`cdn.jsdelivr.net`), same as
  the Python version — so the *charts* on the Overview/CPU/RAM/Disk/
  Network tabs need internet the first time (browsers cache it after).
  The new **Local Activity** and **system stats** data themselves don't
  need internet; only that one script tag does. If you want the whole
  dashboard byte-for-byte offline-capable, vendor `chart.js` into
  `internal/server/web/static/` and swap the `<script src=...>` tag in
  `templates/index.html` for a local path.
- The dashboard's own login (Cloud/Devices tabs) is separate from the
  C agent's login, exactly like the Python version — it's not the
  agent's DPAPI `session.dat`, it's its own persisted session at
  `dashboard_session.dat`.

## Setting up Google / Microsoft login (one-time, per Supabase project)

Email/password login works out of the box against the default
Supabase project. Google and Microsoft sign-in need a bit of one-time
setup on **your own** Supabase project first, because OAuth requires
registering an app with Google/Microsoft and telling Supabase about
it — there's no way around that step, it isn't something this code
can configure for you.

1. **Google**: in [Google Cloud Console](https://console.cloud.google.com/apis/credentials),
   create an OAuth 2.0 Client ID (type "Web application"). Add
   `https://<your-project-ref>.supabase.co/auth/v1/callback` as an
   authorized redirect URI. Copy the Client ID + Client Secret into
   Supabase Dashboard → Authentication → Providers → Google.
2. **Microsoft**: in [Azure Portal](https://portal.azure.com) →
   App registrations, register an app, add the same
   `https://<your-project-ref>.supabase.co/auth/v1/callback` as a
   redirect URI. Copy the Application (client) ID + a client secret
   into Supabase Dashboard → Authentication → Providers → Azure.
   (Supabase's provider id for Microsoft is "azure" — that's what
   `/auth/oauth/start?provider=azure` in this app uses.)
3. In Supabase Dashboard → Authentication → URL Configuration, add
   `http://127.0.0.1:5000/auth/oauth/callback` (or whatever port
   `JUSTINTIME_PORT` is set to) to **Redirect URLs** — Supabase
   refuses to redirect back anywhere not on this allow-list.

Once that's done, the "Continue with Google" / "Continue with
Microsoft" buttons on `/login` work immediately — no code changes
needed. See `handleOAuthStart` / `handleOAuthCallback` /
`handleOAuthComplete` in `internal/server/server.go` for how the
redirect + token handoff works.

