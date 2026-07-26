//go:build !windows

package dashsession

import (
	"encoding/json"
	"os"
	"path/filepath"

	"justintime-dashboard/internal/config"
)

// This build has no DPAPI available, so it stores the session as plain
// JSON. It exists only so the dashboard can be built/tested off Windows
// during development; the project targets Windows, like the rest of
// JustInTime.
func path() string {
	return filepath.Join(config.Dir(), "dashboard_session.json")
}

func Save(s *Session) error {
	b, err := json.Marshal(s)
	if err != nil {
		return err
	}
	return os.WriteFile(path(), b, 0o600)
}

func Load() (*Session, error) {
	b, err := os.ReadFile(path())
	if err != nil {
		return nil, err
	}
	var s Session
	if err := json.Unmarshal(b, &s); err != nil {
		return nil, err
	}
	return &s, nil
}

func Clear() error {
	err := os.Remove(path())
	if err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}
