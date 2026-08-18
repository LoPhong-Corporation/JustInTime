// Package dashsettings is a straight port of dashboard_settings.py.
// It reads/writes the exact same file
// (%APPDATA%\JustInTime\dashboard_settings.json) with the exact same
// shape, so switching from the previous Python dashboard to this Go one
// carries over the user's font/language/alert-threshold preferences
// automatically.
package dashsettings

import (
	"encoding/base64"
	"encoding/json"
	"os"
	"path/filepath"

	"justintime-dashboard/internal/config"
	"justintime-dashboard/internal/secure"
)

type Settings struct {
	Font          string `json:"font"`
	Language      string `json:"language"`
	CPUThreshold  int    `json:"cpu_threshold"`
	RAMThreshold  int    `json:"ram_threshold"`
	DiskThreshold int    `json:"disk_threshold"`

	// TimeFormat: "24h" (mặc định) hoặc "12h" - áp dụng cho thước giờ
	// và tooltip trên Activity Timeline (mục Local Activity).
	TimeFormat string `json:"time_format"`

	// DefaultPeriod: khoảng thời gian mặc định khi mở tab Local
	// Activity - "today" (mặc định), "yesterday", "this_week",
	// "last_week". Không hỗ trợ "custom" làm mặc định (cần ngày cụ
	// thể, không có ý nghĩa cố định).
	DefaultPeriod string `json:"default_period"`

	// Only ever sent to generativelanguage.googleapis.com when the
	// user explicitly clicks "Generate Insights" — never used
	// automatically, never sent anywhere else. Held in memory as
	// plaintext (needed to actually call the API), but on disk it's
	// DPAPI-encrypted (see GeminiAPIKeyEnc below) — this field is
	// never marshaled to JSON itself, only used at runtime.
	GeminiAPIKey string `json:"-"`
}

// on-disk shape: the encrypted key lives under a different field name
// than the old plaintext one ever did, so there's no chance of an old
// build round-tripping a plaintext key back in by accident.
type onDiskSettings struct {
	Font          string `json:"font"`
	Language      string `json:"language"`
	CPUThreshold  int    `json:"cpu_threshold"`
	RAMThreshold  int    `json:"ram_threshold"`
	DiskThreshold int    `json:"disk_threshold"`
	TimeFormat    string `json:"time_format"`
	DefaultPeriod string `json:"default_period"`

	// Base64 of the DPAPI-protected key (Windows-user-scoped — see
	// internal/secure). Empty/absent if no key has been set.
	GeminiAPIKeyEnc string `json:"gemini_api_key_enc,omitempty"`
}

// HasGeminiAPIKey reports whether a key is currently saved, without
// exposing the key itself — used by settings.html so the page can say
// "a key is saved" without ever printing the actual secret into HTML.
func (s Settings) HasGeminiAPIKey() bool {
	return s.GeminiAPIKey != ""
}

func Defaults() Settings {
	return Settings{
		Font:          "sans",
		Language:      "en",
		CPUThreshold:  85,
		RAMThreshold:  85,
		DiskThreshold: 90,
		TimeFormat:    "24h",
		DefaultPeriod: "today",
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
	if v, ok := data["time_format"]; ok {
		_ = json.Unmarshal(v, &out.TimeFormat)
	}
	if v, ok := data["default_period"]; ok {
		_ = json.Unmarshal(v, &out.DefaultPeriod)
	}

	if v, ok := data["gemini_api_key_enc"]; ok {
		var encB64 string
		if err := json.Unmarshal(v, &encB64); err == nil && encB64 != "" {
			if enc, err := base64.StdEncoding.DecodeString(encB64); err == nil {
				if dec, err := secure.Unprotect(enc); err == nil {
					out.GeminiAPIKey = string(dec)
				}
				// Lỗi giải mã (vd đổi máy/đổi user Windows) im lặng bỏ
				// qua - out.GeminiAPIKey vẫn rỗng, người dùng chỉ cần
				// nhập lại key trong Settings, không crash cả app.
			}
		}
	} else if v, ok := data["gemini_api_key"]; ok {
		// MIGRATION (bảo mật): bản build cũ lưu key dạng PLAINTEXT ở
		// trường "gemini_api_key" - đọc 1 lần cho không mất key người
		// dùng đã nhập, rồi Save() ngay bên dưới (do handleSettingsSave
		// gọi lại sau mỗi lần load trong luồng bình thường) sẽ ghi lại
		// dưới dạng mã hoá và bỏ hẳn trường plaintext cũ. Nếu người
		// dùng không vào lại trang Settings, file trên đĩa vẫn giữ
		// nguyên plaintext cho tới lần Save() kế tiếp - không tệ hơn
		// hiện trạng, nhưng khuyến khích mở Settings 1 lần sau khi
		// cập nhật để hoàn tất việc mã hoá.
		_ = json.Unmarshal(v, &out.GeminiAPIKey)
	} else if v, ok := data["anthropic_api_key"]; ok {
		// Migration note: this field switched from Anthropic to
		// Gemini. If an old key is sitting here it's not usable with
		// the Gemini API anyway, so we don't carry it over — just
		// avoid crashing on the old field name.
		var old string
		_ = json.Unmarshal(v, &old)
	}
	return out
}

func Save(s Settings) error {
	disk := onDiskSettings{
		Font:          s.Font,
		Language:      s.Language,
		CPUThreshold:  s.CPUThreshold,
		RAMThreshold:  s.RAMThreshold,
		DiskThreshold: s.DiskThreshold,
		TimeFormat:    s.TimeFormat,
		DefaultPeriod: s.DefaultPeriod,
	}

	if s.GeminiAPIKey != "" {
		enc, err := secure.Protect([]byte(s.GeminiAPIKey))
		if err != nil {
			return err
		}
		disk.GeminiAPIKeyEnc = base64.StdEncoding.EncodeToString(enc)
	}

	b, err := json.MarshalIndent(disk, "", "  ")
	if err != nil {
		return err
	}
	_ = os.MkdirAll(config.Dir(), 0o755)
	return os.WriteFile(path(), b, 0o600)
}
