// Package config centralizes paths and constants. All paths deliberately
// match what the C agent (settings.c) and the previous Python dashboard
// (dashboard_settings.py) already use under %APPDATA%\JustInTime, so this
// Go rewrite is a drop-in replacement: same config dir, same
// justintime.db, same dashboard_settings.json (font/language/thresholds
// carry over automatically), same Supabase project.
package config

import (
	"os"
	"path/filepath"
	"runtime"
)

const (
	// DefaultPort matches the previous Flask dashboard's port so any
	// existing bookmarks/shortcuts keep working.
	DefaultPort = 5000

	SupabaseURL     = "https://crdvfasjtrfrasqehwkc.supabase.co"
	SupabaseAnonKey = "sb_publishable_2BDazw0ggLN0GC9Zyu2hOQ_XrcqaR7v"
)

type Config struct {
	Port        int
	ConfigDir   string
	LocalDBPath string
	DeviceID    string
	SupabaseURL string
	SupabaseKey string
}

// Dir mirrors settings_get_config_dir() in the C agent (%APPDATA%\JustInTime).
func Dir() string {
	if runtime.GOOS == "windows" {
		if appdata := os.Getenv("APPDATA"); appdata != "" {
			return filepath.Join(appdata, "JustInTime")
		}
	}
	// Fallback used only when developing/testing off Windows.
	home, _ := os.UserHomeDir()
	return filepath.Join(home, ".justintime")
}

// deviceID mirrors get_device_id() in the C agent: the Windows computer
// name (os.Hostname() returns the same NetBIOS name on Windows), so
// device-to-device messages line up with the device_id already stamped
// on every activity_logs row.
func deviceID() string {
	if v := os.Getenv("JUSTINTIME_DEVICE_ID"); v != "" {
		return v
	}
	host, err := os.Hostname()
	if err != nil || host == "" {
		return "UNKNOWN-DEVICE"
	}
	return host
}

// Load resolves configuration for this run. Two env vars are supported
// for local testing off Windows: JUSTINTIME_DB_PATH (point at a specific
// SQLite file) and JUSTINTIME_PORT.
func Load() *Config {
	dir := Dir()
	_ = os.MkdirAll(dir, 0o755)

	dbPath := os.Getenv("JUSTINTIME_DB_PATH")
	if dbPath == "" {
		dbPath = filepath.Join(dir, "justintime.db")
	}

	port := DefaultPort
	if v := os.Getenv("JUSTINTIME_PORT"); v != "" {
		if p, err := parsePort(v); err == nil {
			port = p
		}
	}

	return &Config{
		Port:        port,
		ConfigDir:   dir,
		LocalDBPath: dbPath,
		DeviceID:    deviceID(),
		SupabaseURL: SupabaseURL,
		SupabaseKey: SupabaseAnonKey,
	}
}

func parsePort(s string) (int, error) {
	n := 0
	for _, c := range s {
		if c < '0' || c > '9' {
			return 0, os.ErrInvalid
		}
		n = n*10 + int(c-'0')
	}
	return n, nil
}
