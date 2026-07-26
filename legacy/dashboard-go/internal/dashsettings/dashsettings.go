// Package dashsettings is a straight port of dashboard_settings.py.
// It reads/writes the exact same file
// (%APPDATA%\JustInTime\dashboard_settings.json) with the exact same
// shape, so switching from the previous Python dashboard to this Go one
// carries over the user's font/language/alert-threshold preferences
// automatically.
package dashsettings

import (
	"encoding/json"
	"os"
	"path/filepath"

	"justintime-dashboard/internal/config"
)

type Settings struct {
	Font          string `json:"font"`
	Language      string `json:"language"`
	CPUThreshold  int    `json:"cpu_threshold"`
	RAMThreshold  int    `json:"ram_threshold"`
	DiskThreshold int    `json:"disk_threshold"`
}

func Defaults() Settings {
	return Settings{
		Font:          "sans",
		Language:      "en",
		CPUThreshold:  85,
		RAMThreshold:  85,
		DiskThreshold: 90,
	}
}

var FontStacks = map[string]string{
	"mono":  "'Cascadia Code', 'Cascadia Mono', 'Consolas', 'SFMono-Regular', monospace",
	"sans":  "'Segoe UI', 'Inter', system-ui, -apple-system, sans-serif",
	"serif": "'Georgia', 'Times New Roman', serif",
}

func path() string {
	return filepath.Join(config.Dir(), "dashboard_settings.json")
}

func Load() Settings {
	out := Defaults()

	b, err := os.ReadFile(path())
	if err != nil {
		return out
	}

	var data map[string]json.RawMessage
	if err := json.Unmarshal(b, &data); err != nil {
		return out
	}

	if v, ok := data["font"]; ok {
		_ = json.Unmarshal(v, &out.Font)
	}
	if v, ok := data["language"]; ok {
		_ = json.Unmarshal(v, &out.Language)
	}
	if v, ok := data["cpu_threshold"]; ok {
		_ = json.Unmarshal(v, &out.CPUThreshold)
	}
	if v, ok := data["ram_threshold"]; ok {
		_ = json.Unmarshal(v, &out.RAMThreshold)
	}
	if v, ok := data["disk_threshold"]; ok {
		_ = json.Unmarshal(v, &out.DiskThreshold)
	}
	return out
}

func Save(s Settings) error {
	b, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}
	_ = os.MkdirAll(config.Dir(), 0o755)
	return os.WriteFile(path(), b, 0o644)
}
