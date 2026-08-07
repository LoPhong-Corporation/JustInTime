// Package config centralizes paths and constants. All paths deliberately
// match what the C agent (settings.c) and the previous Python dashboard
// (dashboard_settings.py) already use under %APPDATA%\JustInTime, so this
// Go rewrite is a drop-in replacement: same config dir, same
// justintime.db, same dashboard_settings.json (font/language/thresholds
// carry over automatically), same Supabase project.
package config

import (
	"bufio"
	"crypto/rand"
	"fmt"
	"os"
	"path/filepath"
	"runtime"
	"strings"
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
	DeviceLabel string
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

// deviceIdentity reads (or creates) %APPDATA%\JustInTime\device.id -
// the SAME file src/shared/device.c writes on the C agent side. Format
// is 2 lines: the device_id ("PC-XXXXXXXX", a random id that carries
// no personal info - it is NOT the Windows computer name anymore, see
// device.c for why) and an optional user-chosen friendly label.
//
// Whichever of the two processes (agent or dashboard) runs first on a
// given machine creates the file; the other one just reads it back, so
// device_id always matches between them (needed for device-to-device
// messaging, which is keyed by device_id).
func deviceIdentity(dir string) (id string, label string) {
	if v := os.Getenv("JUSTINTIME_DEVICE_ID"); v != "" {
		return v, ""
	}

	path := filepath.Join(dir, "device.id")

	if f, err := os.Open(path); err == nil {
		defer f.Close()

		scanner := bufio.NewScanner(f)
		if scanner.Scan() {
			line := strings.TrimSpace(scanner.Text())
			if strings.HasPrefix(line, "PC-") && len(line) == 11 {
				id = line
			}
		}
		if scanner.Scan() {
			label = strings.TrimSpace(scanner.Text())
		}
	}

	if id == "" {
		id = generateDeviceID()
		// Best-effort write; if it fails (e.g. read-only dir), we still
		// have a usable id for this run, it just won't persist.
		_ = os.WriteFile(path, []byte(id+"\n"+label+"\n"), 0o644)
	}

	return id, label
}

// generateDeviceID mirrors generate_random_id() in the C agent's
// device.c: 4 cryptographically random bytes, formatted as "PC-XXXXXXXX".
func generateDeviceID() string {
	var b [4]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "UNKNOWN-DEVICE"
	}
	return fmt.Sprintf("PC-%02X%02X%02X%02X", b[0], b[1], b[2], b[3])
}

// DisplayLabel returns the friendly name to show in the UI: the
// user-chosen label if one was set, otherwise a generic placeholder
// derived from the device id - NEVER the real Windows computer name,
// to avoid leaking it to anyone this account is shared/linked with
// (e.g. a linked parent account).
//
// FIX (staleness / "dữ liệu máy không đồng bộ"): trước đây đọc
// c.DeviceLabel - 1 field được nạp DUY NHẤT LÚC KHỞI ĐỘNG (Load()).
// Nếu người dùng đổi tên máy sau đó (từ tray, hoặc sửa thẳng file),
// dashboard đang chạy sẽ không bao giờ biết, và cứ gửi mãi cái tên
// CŨ lên Supabase cho tới khi restart app - máy khác thấy tên máy
// "đứng yên", không đồng bộ. Giờ đọc lại thẳng từ device.id mỗi lần
// gọi (chỉ là 1 lần đọc file nhỏ ~vài chục byte, không đáng kể vì
// hàm này chỉ được gọi mỗi 30s trong heartbeatLoop, không phải mỗi
// request).
func (c *Config) DisplayLabel() string {
	if _, label := deviceIdentity(c.ConfigDir); label != "" {
		return label
	}
	if c.DeviceLabel != "" {
		return c.DeviceLabel
	}
	suffix := c.DeviceID
	if len(suffix) > 4 {
		suffix = suffix[len(suffix)-4:]
	}
	return "Computer " + suffix
}

// SetDeviceLabel persists a user-chosen friendly label for this device,
// writing it to the same device.id file the C agent reads.
func SetDeviceLabel(dir, id, label string) error {
	path := filepath.Join(dir, "device.id")
	return os.WriteFile(path, []byte(id+"\n"+label+"\n"), 0o644)
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

	id, label := deviceIdentity(dir)

	return &Config{
		Port:        port,
		ConfigDir:   dir,
		LocalDBPath: dbPath,
		DeviceID:    id,
		DeviceLabel: label,
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
