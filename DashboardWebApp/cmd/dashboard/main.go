// Command dashboard is the entry point for the rewritten JustInTime
// dashboard. It starts the local HTTP server (embedding its own web
// assets, so it needs no separate install step) and, on Windows, shows
// a tray icon with an "Open Dashboard" button. Pass --tray when
// launching at login (this is what internal/autostart registers).
package main

import (
	"flag"
	"fmt"
	"log"
	"net"
	"net/http"
	"os/exec"
	"runtime"

	"justintime-dashboard/internal/config"
	"justintime-dashboard/internal/localdb"
	"justintime-dashboard/internal/server"
	"justintime-dashboard/internal/tray"
)

func main() {
	trayFlag := flag.Bool("tray", false, "show the tray icon (used automatically when launched at login)")
	portFlag := flag.Int("port", 0, "override the local dashboard port")
	flag.Parse()

	cfg := config.Load()
	if *portFlag != 0 {
		cfg.Port = *portFlag
	}

	url := fmt.Sprintf("http://127.0.0.1:%d/", cfg.Port)

	ln, err := net.Listen("tcp", fmt.Sprintf("127.0.0.1:%d", cfg.Port))
	if err != nil {
		// Most likely another instance of the dashboard is already
		// running on this port — just surface it instead of failing.
		log.Printf("dashboard already running (or port %d busy): %v", cfg.Port, err)
		if *trayFlag && runtime.GOOS == "windows" {
			tray.Run(url)
		} else {
			openBrowser(url)
		}
		return
	}

	db, err := localdb.Open(cfg.LocalDBPath)
	if err != nil {
		log.Printf("warning: local database not available yet at %s (%v) — local/offline view will be empty until the agent creates it", cfg.LocalDBPath, err)
	}

	srv := server.New(cfg, db)

	httpServer := &http.Server{Handler: srv}
	go func() {
		if err := httpServer.Serve(ln); err != nil && err != http.ErrServerClosed {
			log.Fatalf("dashboard server error: %v", err)
		}
	}()

	log.Printf("JustInTime Dashboard running at %s", url)

	// Only show the tray icon when explicitly launched with --tray
	// (used by internal/autostart's login entry, or when the user runs
	// the binary directly). When launched as a one-off from the main
	// JustInTime tray menu (no --tray), it just serves + opens the
	// browser and exits its own foreground wait once the browser has
	// what it needs — no second tray icon.
	if *trayFlag && runtime.GOOS == "windows" {
		tray.Run(url)
	} else {
		openBrowser(url)
		select {}
	}
}

func openBrowser(url string) {
	var cmd *exec.Cmd
	switch runtime.GOOS {
	case "windows":
		cmd = exec.Command("cmd", "/c", "start", "", url)
	case "darwin":
		cmd = exec.Command("open", url)
	default:
		cmd = exec.Command("xdg-open", url)
	}
	_ = cmd.Start()
}
