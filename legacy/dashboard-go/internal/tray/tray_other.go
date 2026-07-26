//go:build !windows

package tray

import "log"

// Run has no real tray off Windows (this project targets Windows only,
// like the rest of JustInTime). It just logs the URL so the server can
// still be exercised during development.
func Run(url string) {
	log.Printf("tray not available on this platform — dashboard is running at %s", url)
	select {}
}
