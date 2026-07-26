// Package server is a Go port of app.pyw: it serves the dashboard UI
// and JSON API, switching between cloud data (Supabase, multi-device,
// requires login) and a new local-only mode (this machine's own
// SQLite file, via internal/localdb) that works with zero network and
// zero login — the offline fallback the previous Flask app never had.
package server

import (
	"context"
	"embed"
	"encoding/csv"
	"encoding/json"
	"errors"
	"fmt"
	"html/template"
	"io/fs"
	"log"
	"net/http"
	"sort"
	"strconv"
	"sync"
	"time"

	"justintime-dashboard/internal/cloud"
	"justintime-dashboard/internal/config"
	"justintime-dashboard/internal/dashsession"
	"justintime-dashboard/internal/dashsettings"
	"justintime-dashboard/internal/i18n"
	"justintime-dashboard/internal/localdb"
	"justintime-dashboard/internal/sysstats"
)

//go:embed all:web
var webFS embed.FS

var errNotLoggedIn = errors.New("not logged in")

type Server struct {
	cfg       *config.Config
	db        *localdb.DB
	collector *sysstats.Collector

	mu      sync.Mutex
	session *cloud.Session // nil when not logged in

	mux *http.ServeMux
	tpl *template.Template
}

func New(cfg *config.Config, db *localdb.DB) *Server {
	tplSub, err := fs.Sub(webFS, "web/templates")
	if err != nil {
		log.Fatalf("dashboard: template fs error: %v", err)
	}
	tpl, err := template.New("").Funcs(template.FuncMap{
		"json": func(v any) template.JS {
			b, _ := json.Marshal(v)
			return template.JS(b)
		},
		"css": func(v string) template.CSS {
			// Without this, {{.FontStack}} inside a <style> block gets
			// run through html/template's CSS auto-escaper, which
			// can't verify an arbitrary font-family list as safe and
			// replaces it with a "ZgotmplZ" placeholder — silently
			// breaking the font and falling back to the browser
			// default. Wrapping it in template.CSS marks it trusted
			// (it only ever comes from our own FontStacks map, never
			// user input) so it passes through untouched.
			return template.CSS(v)
		},
		"fmtDuration": func(sec int64) string {
			h := sec / 3600
			m := (sec % 3600) / 60
			s := sec % 60
			return fmt.Sprintf("%02d:%02d:%02d", h, m, s)
		},
		"fmtTime": func(unix int64) string {
			return time.Unix(unix, 0).Format("2006-01-02 15:04:05")
		},
		"truncate": func(n int, s string) string {
			if len(s) > n {
				return s[:n]
			}
			return s
		},
	}).ParseFS(tplSub, "*.html")
	if err != nil {
		log.Fatalf("dashboard: template parse error: %v", err)
	}

	s := &Server{cfg: cfg, db: db, collector: sysstats.NewCollector(), tpl: tpl}

	// Resume a previous login, if any, so the tray-launched, always-on
	// dashboard doesn't force a fresh login after every reboot.
	if sess, err := dashsession.Load(); err == nil {
		s.session = &cloud.Session{
			AccessToken:  sess.AccessToken,
			RefreshToken: sess.RefreshToken,
			UserID:       sess.UserID,
			Email:        sess.Email,
		}
	}

	s.mux = http.NewServeMux()
	s.routes()
	return s
}

func (s *Server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	s.mux.ServeHTTP(w, r)
}

func (s *Server) routes() {
	staticSub, err := fs.Sub(webFS, "web/static")
	if err != nil {
		log.Fatalf("dashboard: static fs error: %v", err)
	}
	s.mux.Handle("/static/", http.StripPrefix("/static/", http.FileServer(http.FS(staticSub))))

	s.mux.HandleFunc("/", s.handleIndex)
	s.mux.HandleFunc("/login", s.handleLoginPage)
	s.mux.HandleFunc("/logout", s.handleLogoutPage)
	s.mux.HandleFunc("/settings", s.handleSettingsPage)
	s.mux.HandleFunc("/report", s.handleReportPage)
	s.mux.HandleFunc("/export/csv", s.handleExportCSV)

	s.mux.HandleFunc("/api/auth/status", s.handleAuthStatus)
	s.mux.HandleFunc("/api/auth/login", s.handleAuthLogin)
	s.mux.HandleFunc("/api/auth/logout", s.handleAuthLogout)

	s.mux.HandleFunc("/api/system/info", s.handleSystemInfo)
	s.mux.HandleFunc("/api/system/network-interfaces", s.handleNetworkInterfaces)
	s.mux.HandleFunc("/api/system/processes", s.handleProcesses)
	s.mux.HandleFunc("/api/system/stream", s.handleSystemStream)

	s.mux.HandleFunc("/api/cloud/summary", s.handleCloudSummary)
	s.mux.HandleFunc("/api/cloud/daily", s.handleCloudDaily)
	s.mux.HandleFunc("/api/cloud/recent", s.handleCloudRecent)

	s.mux.HandleFunc("/api/local/summary", s.handleLocalSummary)
	s.mux.HandleFunc("/api/local/recent", s.handleLocalRecent)

	s.mux.HandleFunc("/api/devices", s.handleDevices)
	s.mux.HandleFunc("/api/inbox", s.handleInbox)
	s.mux.HandleFunc("/api/send", s.handleSend)
	s.mux.HandleFunc("/api/inbox/read", s.handleMarkRead)
}

// ---------------------------------------------------------------------
// Session helpers
// ---------------------------------------------------------------------

func (s *Server) getSession() *cloud.Session {
	s.mu.Lock()
	defer s.mu.Unlock()
	return s.session
}

func (s *Server) setSession(sess *cloud.Session) {
	s.mu.Lock()
	s.session = sess
	s.mu.Unlock()

	_ = dashsession.Save(&dashsession.Session{
		AccessToken:  sess.AccessToken,
		RefreshToken: sess.RefreshToken,
		UserID:       sess.UserID,
		Email:        sess.Email,
	})
}

func (s *Server) clearSession() {
	s.mu.Lock()
	s.session = nil
	s.mu.Unlock()
	_ = dashsession.Clear()
}

// withAuthRetry runs fn against a client built from the current
// session, and if Supabase reports 401, refreshes the token once and
// retries — mirroring the Python app's with_token_refresh decorator.
func withAuthRetry[T any](s *Server, ctx context.Context, fn func(c *cloud.Client) (T, error)) (T, error) {
	var zero T

	sess := s.getSession()
	if sess == nil {
		return zero, errNotLoggedIn
	}

	client := cloud.New(s.cfg.SupabaseURL, s.cfg.SupabaseKey, sess.AccessToken)
	result, err := fn(client)
	if err == nil || !errors.Is(err, cloud.ErrUnauthorized) {
		return result, err
	}

	newSess, rerr := cloud.RefreshSession(ctx, s.cfg.SupabaseURL, s.cfg.SupabaseKey, sess.RefreshToken)
	if rerr != nil {
		s.clearSession()
		return zero, err
	}
	s.setSession(newSess)

	client = cloud.New(s.cfg.SupabaseURL, s.cfg.SupabaseKey, newSess.AccessToken)
	return fn(client)
}

// ---------------------------------------------------------------------
// Page rendering
// ---------------------------------------------------------------------

type pageData struct {
	T            map[string]string
	DS           dashsettings.Settings
	FontStack    string
	CurrentEmail string
	LoggedIn     bool
	DeviceID     string
	ActivePage   string
	Extra        map[string]any
}

func (s *Server) basePageData(activePage string) pageData {
	ds := dashsettings.Load()
	email := ""
	loggedIn := false
	if sess := s.getSession(); sess != nil {
		email = sess.Email
		loggedIn = true
	}
	return pageData{
		T:            i18n.Dict(ds.Language),
		DS:           ds,
		FontStack:    dashsettings.FontStacks[ds.Font],
		CurrentEmail: email,
		LoggedIn:     loggedIn,
		DeviceID:     s.cfg.DeviceID,
		ActivePage:   activePage,
		Extra:        map[string]any{},
	}
}

func (s *Server) render(w http.ResponseWriter, name string, data pageData) {
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	if err := s.tpl.ExecuteTemplate(w, name, data); err != nil {
		http.Error(w, err.Error(), http.StatusInternalServerError)
	}
}

func (s *Server) handleIndex(w http.ResponseWriter, r *http.Request) {
	if r.URL.Path != "/" {
		http.NotFound(w, r)
		return
	}
	s.render(w, "index.html", s.basePageData("dashboard"))
}

func (s *Server) handleLogoutPage(w http.ResponseWriter, r *http.Request) {
	s.clearSession()
	http.Redirect(w, r, "/", http.StatusSeeOther)
}

// handleLoginPage is a classic (non-AJAX) form login, same UX as the
// previous Flask app's /login route: this is the dashboard's own
// login (Supabase email+password), separate from the C agent's login.
// Unlike the previous version, logging in is now optional — the
// dashboard works locally without it — so this page is only reached
// when the user chooses to view cloud/multi-device data.
func (s *Server) handleLoginPage(w http.ResponseWriter, r *http.Request) {
	data := s.basePageData("login")

	if r.Method == http.MethodGet {
		s.render(w, "login.html", data)
		return
	}
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "GET/POST only")
		return
	}

	if err := r.ParseForm(); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}
	email := r.FormValue("email")
	password := r.FormValue("password")
	if email == "" || password == "" {
		data.Extra["error"] = "Please enter both email and password."
		s.render(w, "login.html", data)
		return
	}

	sess, err := cloud.Login(r.Context(), s.cfg.SupabaseURL, s.cfg.SupabaseKey, email, password)
	if err != nil {
		data.Extra["error"] = err.Error()
		s.render(w, "login.html", data)
		return
	}

	s.setSession(sess)
	http.Redirect(w, r, "/?view=cloud", http.StatusSeeOther)
}

// ---------------------------------------------------------------------
// JSON helpers
// ---------------------------------------------------------------------

func writeJSON(w http.ResponseWriter, v any) {
	w.Header().Set("Content-Type", "application/json")
	_ = json.NewEncoder(w).Encode(v)
}

func writeErr(w http.ResponseWriter, code int, msg string) {
	w.Header().Set("Content-Type", "application/json")
	w.WriteHeader(code)
	_ = json.NewEncoder(w).Encode(map[string]string{"error": msg})
}

// ---------------------------------------------------------------------
// Auth API (new: AJAX login/logout/status; the dashboard's own login,
// separate from the C agent's session — same as the previous Flask app)
// ---------------------------------------------------------------------

func (s *Server) handleAuthStatus(w http.ResponseWriter, r *http.Request) {
	sess := s.getSession()
	writeJSON(w, map[string]any{
		"logged_in": sess != nil,
		"email":     emailOf(sess),
	})
}

func emailOf(s *cloud.Session) string {
	if s == nil {
		return ""
	}
	return s.Email
}

type loginRequest struct {
	Email    string `json:"email"`
	Password string `json:"password"`
}

func (s *Server) handleAuthLogin(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req loginRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.Email == "" || req.Password == "" {
		writeErr(w, http.StatusBadRequest, "email and password are required")
		return
	}

	sess, err := cloud.Login(r.Context(), s.cfg.SupabaseURL, s.cfg.SupabaseKey, req.Email, req.Password)
	if err != nil {
		writeErr(w, http.StatusUnauthorized, err.Error())
		return
	}
	s.setSession(sess)
	writeJSON(w, map[string]any{"ok": true, "email": sess.Email})
}

func (s *Server) handleAuthLogout(w http.ResponseWriter, r *http.Request) {
	s.clearSession()
	writeJSON(w, map[string]any{"ok": true})
}

// ---------------------------------------------------------------------
// System stats API (unchanged contract from the Python version, so
// dashboard.js works as-is)
// ---------------------------------------------------------------------

func (s *Server) handleSystemInfo(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, sysstats.GetMachineInfo())
}

func (s *Server) handleNetworkInterfaces(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, sysstats.GetNetworkInterfaces())
}

func (s *Server) handleProcesses(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, map[string]any{"processes": sysstats.GetProcesses(30)})
}

func (s *Server) handleSystemStream(w http.ResponseWriter, r *http.Request) {
	flusher, ok := w.(http.Flusher)
	if !ok {
		http.Error(w, "streaming unsupported", http.StatusInternalServerError)
		return
	}

	w.Header().Set("Content-Type", "text/event-stream")
	w.Header().Set("Cache-Control", "no-cache")
	w.Header().Set("Connection", "keep-alive")

	ticker := time.NewTicker(1 * time.Second)
	defer ticker.Stop()

	for {
		select {
		case <-r.Context().Done():
			return
		case <-ticker.C:
			ds := dashsettings.Load()
			live := s.collector.Live()

			// Field names/shape here must match dashboard.js exactly
			// (data.cpu_alert, data.ram_alert, data.disk_alert, plus
			// all of sysstats.LiveStats flattened) — this mirrors what
			// the Python /api/system/stream route added on top of
			// system_stats.get_live_stats().
			payload := struct {
				sysstats.LiveStats
				CPUAlert      bool `json:"cpu_alert"`
				RAMAlert      bool `json:"ram_alert"`
				DiskAlert     bool `json:"disk_alert"`
				CPUThreshold  int  `json:"cpu_threshold"`
				RAMThreshold  int  `json:"ram_threshold"`
				DiskThreshold int  `json:"disk_threshold"`
			}{
				LiveStats:     live,
				CPUAlert:      live.CPUPercent >= float64(ds.CPUThreshold),
				RAMAlert:      live.RAMPercent >= float64(ds.RAMThreshold),
				DiskAlert:     live.DiskPercent >= float64(ds.DiskThreshold),
				CPUThreshold:  ds.CPUThreshold,
				RAMThreshold:  ds.RAMThreshold,
				DiskThreshold: ds.DiskThreshold,
			}

			b, _ := json.Marshal(payload)
			fmt.Fprintf(w, "data: %s\n\n", b)
			flusher.Flush()
		}
	}
}

// ---------------------------------------------------------------------
// Cloud data API (port of the Python /api/cloud/* routes)
// ---------------------------------------------------------------------

func dayRangeFor(rangeParam string) (string, string) {
	now := time.Now()
	today := now.Format("2006-01-02")
	switch rangeParam {
	case "week":
		return now.AddDate(0, 0, -6).Format("2006-01-02"), today
	case "month":
		return now.AddDate(0, 0, -29).Format("2006-01-02"), today
	default: // "today"
		return today, today
	}
}

func (s *Server) handleCloudSummary(w http.ResponseWriter, r *http.Request) {
	from, to := dayRangeFor(r.URL.Query().Get("range"))

	totals, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.DailyTotal, error) {
		return c.DailyTotals(r.Context(), from, to)
	})
	if err != nil {
		s.writeCloudErr(w, err)
		return
	}

	byApp := map[string]int64{}
	for _, t := range totals {
		byApp[t.ProcessName] += t.TotalSeconds
	}
	type appUsage struct {
		ProcessName  string `json:"process_name"`
		TotalSeconds int64  `json:"total_seconds"`
	}
	var apps []appUsage
	for name, secs := range byApp {
		apps = append(apps, appUsage{ProcessName: name, TotalSeconds: secs})
	}
	sort.Slice(apps, func(i, j int) bool { return apps[i].TotalSeconds > apps[j].TotalSeconds })
	if len(apps) > 15 {
		apps = apps[:15]
	}

	writeJSON(w, map[string]any{"mode": "cloud", "apps": apps})
}

func (s *Server) handleCloudDaily(w http.ResponseWriter, r *http.Request) {
	rangeParam := r.URL.Query().Get("range")
	if rangeParam == "" {
		rangeParam = "week"
	}
	from, to := dayRangeFor(rangeParam)

	totals, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.DailyTotal, error) {
		return c.DailyTotals(r.Context(), from, to)
	})
	if err != nil {
		s.writeCloudErr(w, err)
		return
	}

	byDay := map[string]int64{}
	for _, t := range totals {
		byDay[t.Day] += t.TotalSeconds
	}
	var days []string
	for d := range byDay {
		days = append(days, d)
	}
	sort.Strings(days)

	var values []int64
	for _, d := range days {
		values = append(values, byDay[d])
	}

	writeJSON(w, map[string]any{"mode": "cloud", "days": days, "totals": values})
}

func (s *Server) handleCloudRecent(w http.ResponseWriter, r *http.Request) {
	logs, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.RecentLog, error) {
		return c.RecentLogs(r.Context(), 100)
	})
	if err != nil {
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"mode": "cloud", "records": logs})
}

func (s *Server) writeCloudErr(w http.ResponseWriter, err error) {
	if errors.Is(err, errNotLoggedIn) {
		writeErr(w, http.StatusUnauthorized, "not logged in")
		return
	}
	writeErr(w, http.StatusBadGateway, err.Error())
}

// ---------------------------------------------------------------------
// Local (offline) data API — new, has no Python equivalent
// ---------------------------------------------------------------------

func (s *Server) handleLocalSummary(w http.ResponseWriter, r *http.Request) {
	usage, err := s.db.Usage(0)
	if err != nil {
		writeErr(w, http.StatusServiceUnavailable, err.Error())
		return
	}
	writeJSON(w, map[string]any{"mode": "local", "usage": usage})
}

func (s *Server) handleLocalRecent(w http.ResponseWriter, r *http.Request) {
	recent, err := s.db.RecentActivities(50)
	if err != nil {
		writeErr(w, http.StatusServiceUnavailable, err.Error())
		return
	}
	writeJSON(w, map[string]any{"mode": "local", "recent": recent})
}

// ---------------------------------------------------------------------
// Device-to-device messaging API — new, has no Python equivalent.
// Only ever relayed through Supabase; never a direct connection.
// ---------------------------------------------------------------------

func (s *Server) handleDevices(w http.ResponseWriter, r *http.Request) {
	totals, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.DailyTotal, error) {
		return c.DailyTotals(r.Context(), "", "")
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeJSON(w, map[string]any{"devices": []string{}, "logged_in": false})
			return
		}
		s.writeCloudErr(w, err)
		return
	}

	var others []string
	for _, d := range cloud.Devices(totals) {
		if d != s.cfg.DeviceID {
			others = append(others, d)
		}
	}
	writeJSON(w, map[string]any{"devices": others, "logged_in": true})
}

func (s *Server) handleInbox(w http.ResponseWriter, r *http.Request) {
	msgs, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.Message, error) {
		return c.Inbox(r.Context(), s.cfg.DeviceID, 50)
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeJSON(w, map[string]any{"messages": []cloud.Message{}, "logged_in": false})
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"messages": msgs, "logged_in": true})
}

type sendRequest struct {
	TargetDeviceID string `json:"target_device_id"`
	Kind           string `json:"kind"`
	Payload        string `json:"payload"`
}

func (s *Server) handleSend(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req sendRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErr(w, http.StatusBadRequest, "invalid request body")
		return
	}
	if req.TargetDeviceID == "" || (req.Kind != "message" && req.Kind != "data") {
		writeErr(w, http.StatusBadRequest, "target_device_id and a valid kind (message|data) are required")
		return
	}

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.SendMessage(r.Context(), cloud.Message{
			SenderDeviceID: s.cfg.DeviceID,
			TargetDeviceID: req.TargetDeviceID,
			Kind:           req.Kind,
			Payload:        req.Payload,
		})
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeErr(w, http.StatusServiceUnavailable, "device messaging requires an internet connection and login")
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

type readRequest struct {
	ID int64 `json:"id"`
}

func (s *Server) handleMarkRead(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req readRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErr(w, http.StatusBadRequest, "invalid request body")
		return
	}

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.MarkRead(r.Context(), req.ID)
	})
	if err != nil {
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

// ---------------------------------------------------------------------
// Settings page (port of the Python /settings route)
// ---------------------------------------------------------------------

func (s *Server) handleSettingsPage(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodGet {
		data := s.basePageData("settings")
		s.render(w, "settings.html", data)
		return
	}
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "GET/POST only")
		return
	}

	if err := r.ParseForm(); err != nil {
		http.Error(w, err.Error(), http.StatusBadRequest)
		return
	}

	action := r.FormValue("action")
	data := s.basePageData("settings")

	switch action {
	case "appearance":
		ds := dashsettings.Load()
		if v := r.FormValue("font"); v != "" {
			ds.Font = v
		}
		if v := r.FormValue("language"); v != "" {
			ds.Language = v
		}
		if v, err := strconv.Atoi(r.FormValue("cpu_threshold")); err == nil {
			ds.CPUThreshold = v
		}
		if v, err := strconv.Atoi(r.FormValue("ram_threshold")); err == nil {
			ds.RAMThreshold = v
		}
		if v, err := strconv.Atoi(r.FormValue("disk_threshold")); err == nil {
			ds.DiskThreshold = v
		}
		_ = dashsettings.Save(ds)
		data = s.basePageData("settings")
		data.Extra["message"] = data.T["save_success"]

	case "password":
		newPass := r.FormValue("new_password")
		confirm := r.FormValue("confirm_password")
		if newPass != confirm {
			data.Extra["error"] = data.T["password_mismatch"]
		} else {
			_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
				return struct{}{}, c.ChangePassword(r.Context(), newPass)
			})
			if err != nil {
				data.Extra["error"] = err.Error()
			} else {
				data.Extra["message"] = data.T["password_changed"]
			}
		}
	}

	s.render(w, "settings.html", data)
}

// ---------------------------------------------------------------------
// Report / print page + CSV export (port of Python /report, /export/csv)
// ---------------------------------------------------------------------

func (s *Server) handleReportPage(w http.ResponseWriter, r *http.Request) {
	rangeParam := r.URL.Query().Get("range")
	from, to := dayRangeFor(rangeParam)

	data := s.basePageData("report")

	totals, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.DailyTotal, error) {
		return c.DailyTotals(r.Context(), from, to)
	})
	if err != nil {
		data.Extra["error"] = err.Error()
		s.render(w, "report_print.html", data)
		return
	}

	records, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.RecentLog, error) {
		return c.RecentLogs(r.Context(), 500)
	})
	if err != nil {
		records = nil
	}
	data.Extra["records"] = records

	byApp := map[string]int64{}
	for _, t := range totals {
		byApp[t.ProcessName] += t.TotalSeconds
	}
	type appUsage struct {
		ProcessName  string
		TotalSeconds int64
	}
	var apps []appUsage
	for name, secs := range byApp {
		apps = append(apps, appUsage{ProcessName: name, TotalSeconds: secs})
	}
	sort.Slice(apps, func(i, j int) bool { return apps[i].TotalSeconds > apps[j].TotalSeconds })

	data.Extra["range_key"] = rangeParam
	data.Extra["from"] = from
	data.Extra["to"] = to
	data.Extra["apps"] = apps
	data.Extra["generated_at"] = time.Now().Format("2006-01-02 15:04:05")

	s.render(w, "report_print.html", data)
}

func (s *Server) handleExportCSV(w http.ResponseWriter, r *http.Request) {
	logs, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.RecentLog, error) {
		return c.RecentLogs(r.Context(), 1000)
	})
	if err != nil {
		http.Error(w, err.Error(), http.StatusBadGateway)
		return
	}

	w.Header().Set("Content-Type", "text/csv; charset=utf-8")
	w.Header().Set("Content-Disposition", `attachment; filename="justintime_export.csv"`)

	cw := csv.NewWriter(w)
	_ = cw.Write([]string{"device_id", "process_name", "window_title", "duration_seconds", "start_time", "end_time"})
	for _, l := range logs {
		_ = cw.Write([]string{
			l.DeviceID, l.ProcessName, l.WindowTitle,
			fmt.Sprintf("%d", l.Duration),
			time.Unix(l.StartTime, 0).Format(time.RFC3339),
			time.Unix(l.EndTime, 0).Format(time.RFC3339),
		})
	}
	cw.Flush()
}
