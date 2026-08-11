//go:build !windows

package secure

// This build has no DPAPI available. It exists only so the dashboard
// can be built/tested off Windows during development; the project
// targets Windows, like the rest of JustInTime, where the real
// DPAPI-backed version (secure_windows.go) is used instead. This
// passthrough is NOT encryption — don't rely on it for anything real.
func Protect(data []byte) ([]byte, error) {
	out := make([]byte, len(data))
	copy(out, data)
	return out, nil
}

func Unprotect(data []byte) ([]byte, error) {
	out := make([]byte, len(data))
	copy(out, data)
	return out, nil
}
