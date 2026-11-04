//go:build windows

package dashsession

import (
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"unsafe"

	"golang.org/x/sys/windows"

	"justintime-dashboard/internal/config"
)

type dataBlob struct {
	cbData uint32
	pbData *byte
}

var (
	modcrypt32             = windows.NewLazySystemDLL("crypt32.dll")
	modkernel32            = windows.NewLazySystemDLL("kernel32.dll")
	procCryptProtectData   = modcrypt32.NewProc("CryptProtectData")
	procCryptUnprotectData = modcrypt32.NewProc("CryptUnprotectData")
	procLocalFree          = modkernel32.NewProc("LocalFree")
)

func protect(data []byte) ([]byte, error) {
	if len(data) == 0 {
		return nil, fmt.Errorf("empty data")
	}
	in := dataBlob{cbData: uint32(len(data)), pbData: &data[0]}
	var out dataBlob

	descr, _ := windows.UTF16PtrFromString("JustInTime dashboard session")

	r, _, callErr := procCryptProtectData.Call(
		uintptr(unsafe.Pointer(&in)),
		uintptr(unsafe.Pointer(descr)),
		0, 0, 0, 0,
		uintptr(unsafe.Pointer(&out)),
	)
	if r == 0 {
		return nil, fmt.Errorf("CryptProtectData failed: %w", callErr)
	}
	defer procLocalFree.Call(uintptr(unsafe.Pointer(out.pbData)))

	result := make([]byte, out.cbData)
	copy(result, unsafe.Slice(out.pbData, out.cbData))
	return result, nil
}

func unprotect(data []byte) ([]byte, error) {
	if len(data) == 0 {
		return nil, fmt.Errorf("empty data")
	}
	in := dataBlob{cbData: uint32(len(data)), pbData: &data[0]}
	var out dataBlob

	r, _, callErr := procCryptUnprotectData.Call(
		uintptr(unsafe.Pointer(&in)),
		0, 0, 0, 0, 0,
		uintptr(unsafe.Pointer(&out)),
	)
	if r == 0 {
		return nil, fmt.Errorf("CryptUnprotectData failed: %w", callErr)
	}
	defer procLocalFree.Call(uintptr(unsafe.Pointer(out.pbData)))

	result := make([]byte, out.cbData)
	copy(result, unsafe.Slice(out.pbData, out.cbData))
	return result, nil
}

func path() string {
	return filepath.Join(config.Dir(), "dashboard_session.dat")
}

// Save encrypts and writes the session, DPAPI-scoped to the current
// Windows user (same mechanism as auth.c's save_session_to_disk).
func Save(s *Session) error {
	buf := fmt.Sprintf("%s\n%s\n%s\n%s\n", s.UserID, s.Email, s.AccessToken, s.RefreshToken)

	enc, err := protect([]byte(buf))
	if err != nil {
		return err
	}
	return os.WriteFile(path(), enc, 0o600)
}

// Load reads and decrypts the session, if any.
func Load() (*Session, error) {
	enc, err := os.ReadFile(path())
	if err != nil {
		return nil, err
	}

	dec, err := unprotect(enc)
	if err != nil {
		return nil, err
	}

	lines := strings.Split(strings.TrimRight(string(dec), "\n"), "\n")
	if len(lines) < 4 {
		return nil, fmt.Errorf("malformed dashboard_session.dat")
	}

	return &Session{
		UserID:       lines[0],
		Email:        lines[1],
		AccessToken:  lines[2],
		RefreshToken: lines[3],
	}, nil
}

// Clear removes any stored session (logout).
func Clear() error {
	err := os.Remove(path())
	if err != nil && !os.IsNotExist(err) {
		return err
	}
	return nil
}
