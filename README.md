# JustInTime 

> A lightweight native Windows activity monitoring agent written in C++ and Go.

![Platform](https://img.shields.io/badge/Platform-Windows-blue)
![Language](https://img.shields.io/badge/Language-C++-success)
![Framework](https://img.shields.io/badge/Framework-Qt6-success)
![Compiler](https://img.shields.io/badge/Compiler-MSVC-purple)
![Database](https://img.shields.io/badge/Database-SQLite-orange)
![License](https://img.shields.io/badge/License-MIT-green)

JustInTime is a high-performance Windows background agent that monitors user activity, records application usage locally using SQLite, and synchronizes data securely to the cloud through **Supabase Edge Functions** - plus a companion Dashboard app for viewing that data and managing family/parental controls.

The project follows an **offline-first** architecture, ensuring that activity data is never lost even when the device is temporarily offline.

---

## Features

### ✅ Currently Implemented

- Foreground application monitoring
- Active window title detection
- Process name detection
- Activity duration tracking
- SQLite local database
- UTF-16 ⇄ UTF-8 conversion
- Full Unicode support
- Device identification
- JSON serialization
- HTTP communication
- Secure synchronization through Supabase Edge Functions
- Offline queue
- Automatic synchronization
- Unsynced record management

---

# Architecture

```text
                     Windows
                        │
                        ▼
          ┌────────────────────────┐
          │ Activity Monitor       │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │ SQLite Local Database  │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │ Sync Queue             │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │ HTTP Client            │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │ Supabase Edge Function │
          └────────────┬───────────┘
                       │
                       ▼
          ┌────────────────────────┐
          │ PostgreSQL Database    │
          └────────────────────────┘
```

---

# Project Structure

The native agent and Qt Dashboard build as **two separate executables**
that share one small common library. The repository also contains the Go
API and a React/Tauri desktop dashboard:

```text
JustInTime/
├── includes/             All native C++ headers, grouped by component.
│   ├── agent/             Agent-only headers: agent/, core/, parent/, shared/.
│   ├── common/            Headers shared by agent and native dashboard.
│   └── dashboard/         Native dashboard headers, including ui/.
├── src/                   All native C++ implementations, grouped the same way.
│   ├── agent/             Activity tracking, sync, tray, and parent UI.
│   ├── common/            Shared logging, HTTP, and configuration utilities.
│   └── dashboard/         Qt dashboard and its UI.
├── apps/
│   ├── agent/             CMake target for JustInTime.exe.
│   ├── dashboard/         CMake target, tests, and dashboard-only dependencies.
│   └── dashboard-web/
│       ├── backend/       Go API and embedded web dashboard.
│       └── desktop/       React/Tauri desktop app; starts the Go sidecar.
├── libs/common/           CMake definition for the shared native library.
├── third_party/           Shared SQLite dependency; dashboard-only JSON lives
│                          under apps/dashboard/third_party/.
├── migrations/          All Supabase schema and incremental SQL migrations.
├── docs/                Build and contributor documentation.
├── installer/           Inno Setup script and Windows build entry point.
└── supabase/            Edge Functions and Supabase project configuration.
```

### Why two executables instead of one

JustInTime.exe can open the Dashboard from its tray menu ("Open
Dashboard") at any time - it launches `JustInTimeDashboard.exe`, which
sits right next to it after a build (`TrayIcon::onOpenDashboard()` in
`src/agent/agent/trayicon.cpp`). Keeping them separate (rather than
one combined binary) means:

- Each app builds, and can be debugged, **independently** - no need to
  rebuild/relaunch the background agent just to iterate on a dashboard UI
  change, and vice versa.
- A crash or hang in the Dashboard UI can never take the background
  activity-tracking process down with it.
- `libs/common` is the only thing that must stay in sync between them -
  everything else (`src/agent/shared/*`, `src/dashboard/*`) is
  free to diverge without risk of silently affecting the other app. See
  the comment at the top of `libs/common/CMakeLists.txt` for the rule of
  thumb on what belongs there (used by *both* apps) versus what belongs
  in one app's own tree (used by *one*).

Because `JustInTimeDashboard.exe` has no HTTP server anymore (the earlier
Go version did), "already running?" is detected with a named mutex +
`FindWindow`/`SetForegroundWindow` instead of a port check - see
`src/dashboard/main.cpp`.

---

# Why Edge Functions?

The client **never communicates directly with the database**.

Instead, every synchronization request is sent to a **Supabase Edge Function**, which is responsible for:

- Request validation
- Authentication
- Data verification
- Business logic
- Database access

This architecture improves security by preventing the client from having direct database access.

---

# Offline First Design

Every activity is stored locally before being synchronized.

```text
Foreground Window

↓

SQLite

↓

Unsynced Queue

↓

HTTP Request

↓

Edge Function

↓

Cloud Database

↓

Mark Record As Synced
```

If the network is unavailable, records remain safely stored in SQLite and will be synchronized automatically once the connection is restored.

---

# Technologies

- C17
- WinAPI
- SQLite3
- WinHTTP
- Supabase Edge Functions
- PostgreSQL
- CMake
- Go

---

# Build

Requirements

- Windows 10 or newer
- Microsoft Visual C++ (MSVC)
- CMake

```bash
git clone https://github.com/LoPhong-Corporation/JustInTime.git

cd JustInTime

mkdir build

cd build

cmake ..

cmake --build .
```

This produces **both** executables (`JustInTime.exe` and
`JustInTimeDashboard.exe`) in the same output folder - see
[Project Structure](#project-structure) above. To build only one of them,
pass `--target JustInTime` or `--target JustInTimeDashboard` to the build
command.


# Building the Windows installer

The Inno Setup script is `installer/JustInTime.iss`. Install CMake, Go,
Node.js/npm, Rust/Cargo, Inno Setup 6, and the Qt 6.11.2 MSVC 2022 x64
kit used by the current CMake configuration. Run `installer\build.cmd`
from an x64 Visual Studio Developer Command Prompt. It builds and
deploys the Qt applications, builds the Go sidecar and Tauri dashboard,
then writes
`installer\dist\JustInTime-Setup-1.0.2.2.exe`.

For native build prerequisites and manual CMake steps, see
[`docs/HOW_TO_BUILD.md`](docs/HOW_TO_BUILD.md), which also documents the
root-level `includes/` and `src/` layout for native C++.

The installer creates shortcuts for the agent, React/Tauri dashboard,
and native Qt dashboard. The Go backend is installed under
`dashboard-go\dashboard.exe` beside the Tauri app. User data under
`%APPDATA%\JustInTime` is retained when uninstalling. Windows must have
the Microsoft Edge WebView2 runtime for the Tauri dashboard.

# Documentation

Project documentation is available in the GitHub Wiki.

Topics include:

- Architecture
- Activity Monitor
- Database
- Sync Engine
- Network Layer
- Configuration
- Development Guide

---

# Contributing

Contributions, issues, and feature requests are welcome.

Please open an Issue before submitting a Pull Request for large changes.

---

# License

This project is licensed under the MIT License.

See the [LICENSE](LICENSE) file for more information.


**Made with ❤️ by LoPhong Corporation**
