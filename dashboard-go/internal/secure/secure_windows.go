//go:build windows

package secure

import (
	"fmt"
	"unsafe"

	"golang.org/x/sys/windows"
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

// Protect encrypts data with DPAPI, scoped to the current Windows user
// (CRYPTPROTECT_UI_FORBIDDEN-free — same call shape dashsession uses).
func Protect(data []byte) ([]byte, error) {
	if len(data) == 0 {
		return nil, fmt.Errorf("empty data")
	}
	in := dataBlob{cbData: uint32(len(data)), pbData: &data[0]}
	var out dataBlob

	descr, _ := windows.UTF16PtrFromString("JustInTime dashboard secret")

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

// Unprotect reverses Protect. Fails if called by a different Windows
// user account than the one that encrypted the data, by design.
func Unprotect(data []byte) ([]byte, error) {
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
