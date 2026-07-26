//go:build !windows

package autostart

// Set is a no-op off Windows — login autostart via the registry Run key
// only makes sense on Windows, matching the rest of this project.
func Set(enabled bool) error { return nil }

// IsEnabled always reports false off Windows.
func IsEnabled() bool { return false }
