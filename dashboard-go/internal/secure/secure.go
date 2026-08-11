// Package secure provides small, at-rest encryption for secrets the
// dashboard has to persist locally — right now just the Gemini API
// key (see dashsettings.go). Before this, the key sat in plain JSON
// in %APPDATA%\JustInTime\dashboard_settings.json, readable by
// anything running as the same Windows user (or by anyone who copies
// the file). Protect()/Unprotect() use the same DPAPI mechanism
// internal/dashsession already relies on for the login session, scoped
// to the current Windows user account — a process running as someone
// else, or the file copied to another machine, can't decrypt it.
package secure
