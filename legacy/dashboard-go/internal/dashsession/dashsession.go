// Package dashsession persists the dashboard's own login (separate from
// the C agent's own session.dat — the dashboard has always had its own
// login screen, see the previous Flask app's /login route) so that the
// tray-launched, always-running dashboard doesn't force a fresh login
// after every reboot. Stored at %APPDATA%\JustInTime\dashboard_session.dat,
// DPAPI-encrypted on Windows the same way auth.c protects the agent's
// own session.
package dashsession

type Session struct {
	AccessToken  string
	RefreshToken string
	UserID       string
	Email        string
}
