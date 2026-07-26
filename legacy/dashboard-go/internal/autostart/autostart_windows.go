//go:build windows

// Package autostart registers the dashboard binary (with its tray icon)
// to launch every time the user logs into Windows — a separate registry
// value from the C agent's own (settings_apply_autostart in settings.c),
// so enabling/disabling one never touches the other.
package autostart

import (
	"fmt"
	"os"

	"golang.org/x/sys/windows/registry"
)

const (
	runKeyPath = `Software\Microsoft\Windows\CurrentVersion\Run`
	valueName  = "JustInTimeDashboard"
)

// Set enables or disables launching the dashboard (with --tray) at login.
func Set(enabled bool) error {
	key, err := registry.OpenKey(registry.CURRENT_USER, runKeyPath, registry.SET_VALUE)
	if err != nil {
		return err
	}
	defer key.Close()

	if !enabled {
		err := key.DeleteValue(valueName)
		if err != nil && err != registry.ErrNotExist {
			return err
		}
		return nil
	}

	exePath, err := os.Executable()
	if err != nil {
		return err
	}
	return key.SetStringValue(valueName, fmt.Sprintf(`"%s" --tray`, exePath))
}

// IsEnabled reports whether the dashboard currently launches at login.
func IsEnabled() bool {
	key, err := registry.OpenKey(registry.CURRENT_USER, runKeyPath, registry.QUERY_VALUE)
	if err != nil {
		return false
	}
	defer key.Close()

	_, _, err = key.GetStringValue(valueName)
	return err == nil
}
