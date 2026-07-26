//go:build windows

// Package tray gives the dashboard a system tray icon with the button
// requested: "Open dashboard" (launches the local dashboard in the
// default browser) plus a toggle for launching automatically every
// time the user logs into Windows.
package tray

import (
	"bytes"
	"encoding/binary"
	"image"
	"image/color"
	"image/png"
	"os/exec"

	"github.com/getlantern/systray"

	"justintime-dashboard/internal/autostart"
)

// Run blocks for the lifetime of the process, showing the tray icon.
// url is opened in the browser when "Open Dashboard" is clicked.
func Run(url string) {
	systray.Run(onReady(url), func() {})
}

func onReady(url string) func() {
	return func() {
		systray.SetIcon(defaultIcon())
		systray.SetTitle("JustInTime")
		systray.SetTooltip("JustInTime Dashboard")

		mOpen := systray.AddMenuItem("Open Dashboard", "Open the local dashboard in your browser")
		systray.AddSeparator()
		mAutostart := systray.AddMenuItem("Launch at login", "Start the dashboard automatically every time you log in")
		if autostart.IsEnabled() {
			mAutostart.Check()
		} else {
			mAutostart.Uncheck()
		}
		systray.AddSeparator()
		mQuit := systray.AddMenuItem("Quit", "Close JustInTime Dashboard")

		// Opening the dashboard once on startup is what satisfies
		// "launch the local dashboard every time you log in" when this
		// binary itself is registered to run at login (see autostart).
		go openBrowser(url)

		go func() {
			for {
				select {
				case <-mOpen.ClickedCh:
					openBrowser(url)

				case <-mAutostart.ClickedCh:
					if mAutostart.Checked() {
						mAutostart.Uncheck()
						_ = autostart.Set(false)
					} else {
						mAutostart.Check()
						_ = autostart.Set(true)
					}

				case <-mQuit.ClickedCh:
					systray.Quit()
					return
				}
			}
		}()
	}
}

func openBrowser(url string) {
	_ = exec.Command("cmd", "/c", "start", "", url).Start()
}

// defaultIcon builds a small solid-color 16x16 icon at runtime (a PNG
// wrapped in a minimal ICO container, supported since Windows Vista),
// so the tray doesn't depend on shipping a separate .ico asset file.
func defaultIcon() []byte {
	img := image.NewRGBA(image.Rect(0, 0, 16, 16))
	brand := color.RGBA{37, 99, 235, 255}
	for y := 0; y < 16; y++ {
		for x := 0; x < 16; x++ {
			img.Set(x, y, brand)
		}
	}

	var pngBuf bytes.Buffer
	_ = png.Encode(&pngBuf, img)
	pngBytes := pngBuf.Bytes()

	var ico bytes.Buffer
	binary.Write(&ico, binary.LittleEndian, uint16(0))             // reserved
	binary.Write(&ico, binary.LittleEndian, uint16(1))             // type: icon
	binary.Write(&ico, binary.LittleEndian, uint16(1))             // image count
	ico.WriteByte(16)                                              // width
	ico.WriteByte(16)                                              // height
	ico.WriteByte(0)                                               // color count
	ico.WriteByte(0)                                               // reserved
	binary.Write(&ico, binary.LittleEndian, uint16(1))             // planes
	binary.Write(&ico, binary.LittleEndian, uint16(32))            // bits per pixel
	binary.Write(&ico, binary.LittleEndian, uint32(len(pngBytes))) // resource size
	binary.Write(&ico, binary.LittleEndian, uint32(6+16))          // offset to image data
	ico.Write(pngBytes)

	return ico.Bytes()
}
