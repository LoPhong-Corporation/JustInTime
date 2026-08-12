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
	"net/url"
	"os"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"

	"justintime-dashboard/internal/aiinsights"
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

	go s.heartbeatLoop()

	return s
}

// heartbeatLoop pushes this device's own CPU/RAM/Disk + a "last seen"
// timestamp to Supabase every 30s, but only while logged in (silently
// does nothing otherwise — this never blocks or slows down the rest
// of the dashboard). This is the only thing that makes the Machines
// list / Overview Online-Offline status possible: two devices on the
// same account simply see each other's heartbeat rows here, still
// entirely relayed through Supabase — never a direct connection.
func (s *Server) heartbeatLoop() {
	// FIX (privacy): trước đây gửi thẳng os.Hostname() (tên máy Windows
	// thật) lên Supabase làm cột "hostname", hiển thị nguyên văn cho bất
	// kỳ ai xem chung tài khoản (kể cả phụ huynh xem máy con). Giờ gửi
	// DisplayLabel() - nhãn ẩn danh mặc định "Computer XXXX" hoặc tên do
	// chính người dùng tự đặt, không bao giờ là tên máy thật.
	label := s.cfg.DisplayLabel()

	push := func() {
		sess := s.getSession()
		if sess == nil {
			return
		}

		live := s.collector.Live()
		client := cloud.New(s.cfg.SupabaseURL, s.cfg.SupabaseKey, sess.AccessToken)

		ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
		defer cancel()

		_ = client.PushHeartbeat(ctx, s.cfg.DeviceID, label, live.CPUPercent, live.RAMPercent, live.DiskPercent)
	}

	push() // đẩy ngay lần đầu, không đợi hết chu kỳ 30s

	ticker := time.NewTicker(30 * time.Second)
	defer ticker.Stop()

	for range ticker.C {
		push()
	}
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
	s.mux.HandleFunc("/auth/oauth/start", s.handleOAuthStart)
	s.mux.HandleFunc("/auth/oauth/callback", s.handleOAuthCallback)
	s.mux.HandleFunc("/auth/oauth/complete", s.handleOAuthComplete)
	s.mux.HandleFunc("/logout", s.handleLogoutPage)
	s.mux.HandleFunc("/settings", s.handleSettingsPage)
	s.mux.HandleFunc("/report", s.handleReportPage)
	s.mux.HandleFunc("/export/csv", s.handleExportCSV)

	s.mux.HandleFunc("/api/auth/status", s.handleAuthStatus)
	s.mux.HandleFunc("/api/auth/login", s.handleAuthLogin)
	s.mux.HandleFunc("/api/auth/logout", s.handleAuthLogout)
	s.mux.HandleFunc("/api/auth/logout-everywhere", s.handleAuthLogoutEverywhere)

	s.mux.HandleFunc("/api/system/info", s.handleSystemInfo)
	s.mux.HandleFunc("/api/system/network-interfaces", s.handleNetworkInterfaces)
	s.mux.HandleFunc("/api/system/processes", s.handleProcesses)
	s.mux.HandleFunc("/api/system/stream", s.handleSystemStream)

	s.mux.HandleFunc("/api/cloud/summary", s.handleCloudSummary)
	s.mux.HandleFunc("/api/cloud/daily", s.handleCloudDaily)
	s.mux.HandleFunc("/api/cloud/recent", s.handleCloudRecent)

	s.mux.HandleFunc("/api/local/summary", s.handleLocalSummary)
	s.mux.HandleFunc("/api/local/recent", s.handleLocalRecent)
	s.mux.HandleFunc("/api/local/timeline", s.handleTimeline)
	s.mux.HandleFunc("/api/local/period", s.handleLocalPeriod)
	s.mux.HandleFunc("/api/local/chat", s.handleChat)
	s.mux.HandleFunc("/api/local/insights", s.handleInsights)
	s.mux.HandleFunc("/api/machines", s.handleMachines)
	s.mux.HandleFunc("/api/machines/remove", s.handleMachineRemove)

	s.mux.HandleFunc("/api/devices", s.handleDevices)
	s.mux.HandleFunc("/api/inbox", s.handleInbox)
	s.mux.HandleFunc("/api/thread", s.handleThread)
	s.mux.HandleFunc("/api/send", s.handleSend)
	s.mux.HandleFunc("/api/inbox/read", s.handleMarkRead)
	s.mux.HandleFunc("/api/devices/share-stats", s.handleShareStats)

	s.mux.HandleFunc("/api/parent/children", s.handleParentChildren)
	s.mux.HandleFunc("/api/parent/parents", s.handleParentParents)
	s.mux.HandleFunc("/api/parent/invite", s.handleParentInvite)
	s.mux.HandleFunc("/api/parent/approve", s.handleParentApprove)
	s.mux.HandleFunc("/api/parent/revoke", s.handleParentRevoke)
	s.mux.HandleFunc("/api/parent/permission", s.handleParentSetPermission)
	s.mux.HandleFunc("/api/parent/child-summary", s.handleParentChildSummary)
	s.mux.HandleFunc("/api/parent/child-heartbeat", s.handleParentChildHeartbeat)
	s.mux.HandleFunc("/api/parent/limits", s.handleParentLimits)
	s.mux.HandleFunc("/api/parent/limits/set", s.handleParentLimitSet)
	s.mux.HandleFunc("/api/parent/limits/delete", s.handleParentLimitDelete)
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
		/*
		 * BUG CŨ ("Warning/Critical không cập nhật", header vẫn hiện
		 * đã đăng nhập nhưng Machines lại đòi đăng nhập lại): trước
		 * đây cứ hễ RefreshSession() lỗi là clearSession() NGAY LẬP
		 * TỨC, KHÔNG PHÂN BIỆT lỗi thật (refresh token bị Supabase
		 * từ chối hẳn - *cloud.AuthError) với lỗi mạng tạm thời
		 * (timeout, đứt mạng vài giây, DNS lag...) - vốn KHÔNG có
		 * nghĩa là phiên đăng nhập đã hỏng. Vì session (s.session)
		 * là biến TOÀN CỤC dùng chung cho MỌI request, chỉ cần 1 lần
		 * gọi API tình cờ trúng lúc mạng chập chờn là XOÁ SẠCH phiên
		 * đăng nhập của CẢ APP - các phần khác (Machines, Family...)
		 * đột nhiên đòi đăng nhập lại dù người dùng chẳng làm gì cả,
		 * trong khi phần header (render lúc tải trang, trước khi bug
		 * này xảy ra) vẫn hiện email cũ cho tới khi F5 lại.
		 *
		 * Giờ CHỈ xoá session khi Supabase THẬT SỰ từ chối refresh
		 * token (trả về *cloud.AuthError, tức có phản hồi HTTP rõ
		 * ràng nói "token này không dùng được nữa") - mọi lỗi khác
		 * (mạng, timeout...) coi là tạm thời, GIỮ NGUYÊN session để
		 * lần gọi API tiếp theo có cơ hội thử lại bình thường.
		 */
		var authErr *cloud.AuthError
		if errors.As(rerr, &authErr) {
			s.clearSession()
		}
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
	DeviceLabel  string
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
		DeviceLabel:  s.cfg.DisplayLabel(),
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
// Google / Microsoft OAuth login (Supabase Auth as the identity broker)
//
// Flow (all still just this local dashboard talking to Supabase - no
// new third party, no change to the C agent's own separate session):
//   1. GET /auth/oauth/start?provider=google|azure  -> 302 to Supabase's
//      GoTrue /authorize endpoint, which redirects to Google/Microsoft.
//   2. Provider redirects back to Supabase, which redirects to OUR
//      redirect_to (/auth/oauth/callback) with the session tokens in
//      the URL FRAGMENT (#access_token=...&refresh_token=...) -
//      fragments never reach the server, only the browser sees them.
//   3. /auth/oauth/callback is a tiny static HTML+JS page: it reads
//      window.location.hash and POSTs the tokens to
//      /auth/oauth/complete, which is a normal server endpoint.
//   4. /auth/oauth/complete verifies the access_token against Supabase
//      (GET /auth/v1/user) and stores the session exactly like email/
//      password login does (s.setSession).
//
// "azure" is Supabase's provider id for Microsoft (Azure AD / Entra
// ID) - there's no separate "microsoft" id.
// ---------------------------------------------------------------------

var oauthProviderNames = map[string]string{
	"google": "google",
	"azure":  "azure",
}

func (s *Server) oauthRedirectURL(r *http.Request) string {
	// Dùng chính Host mà trình duyệt đang gọi (thường 127.0.0.1:PORT
	// hoặc localhost:PORT) thay vì hardcode - để hoạt động đúng dù
	// người dùng đổi cổng qua JUSTINTIME_PORT.
	scheme := "http"
	return fmt.Sprintf("%s://%s/auth/oauth/callback", scheme, r.Host)
}

func (s *Server) handleOAuthStart(w http.ResponseWriter, r *http.Request) {
	provider := r.URL.Query().Get("provider")
	if _, ok := oauthProviderNames[provider]; !ok {
		http.Error(w, "unknown provider", http.StatusBadRequest)
		return
	}

	authorizeURL := fmt.Sprintf(
		"%s/auth/v1/authorize?provider=%s&redirect_to=%s",
		s.cfg.SupabaseURL,
		url.QueryEscape(provider),
		url.QueryEscape(s.oauthRedirectURL(r)),
	)
	http.Redirect(w, r, authorizeURL, http.StatusFound)
}

// handleOAuthCallback serves the tiny landing page that grabs the
// tokens out of the URL fragment (server-side code can never see a
// fragment - only client JS can) and hands them to /auth/oauth/complete.
func (s *Server) handleOAuthCallback(w http.ResponseWriter, r *http.Request) {
	data := s.basePageData("login")
	s.render(w, "oauth_callback.html", data)
}

type oauthCompleteRequest struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	ErrorDesc    string `json:"error_description"`
}

func (s *Server) handleOAuthComplete(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}

	var req oauthCompleteRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErr(w, http.StatusBadRequest, "invalid request body")
		return
	}
	if req.ErrorDesc != "" {
		writeErr(w, http.StatusBadRequest, req.ErrorDesc)
		return
	}

	sess, err := cloud.SessionFromOAuthTokens(r.Context(), s.cfg.SupabaseURL, s.cfg.SupabaseKey, req.AccessToken, req.RefreshToken)
	if err != nil {
		writeErr(w, http.StatusUnauthorized, err.Error())
		return
	}

	s.setSession(sess)
	writeJSON(w, map[string]any{"ok": true})
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
	// SECURITY: trước đây chỉ xoá session cục bộ - access/refresh
	// token vẫn còn hiệu lực bên Supabase, chưa hề bị thu hồi. Giờ
	// gọi /auth/v1/logout trước khi xoá local, để token thật sự bị
	// vô hiệu hoá. Không chặn logout cục bộ nếu bước revoke lỗi
	// (mất mạng chẳng hạn) - người dùng vẫn cần thoát được, chỉ là
	// token cũ có thể còn sống tới khi tự hết hạn.
	if sess := s.getSession(); sess != nil {
		ctx, cancel := context.WithTimeout(r.Context(), 5*time.Second)
		_ = cloud.Logout(ctx, s.cfg.SupabaseURL, s.cfg.SupabaseKey, sess.AccessToken, "")
		cancel()
	}
	s.clearSession()
	writeJSON(w, map[string]any{"ok": true})
}

// handleAuthLogoutEverywhere revokes EVERY refresh token for this
// user (scope=global) — every browser tab, every device, every place
// still logged in — not just this dashboard's own session. Use after
// suspecting a leaked token, a stolen device, or just as routine
// hygiene from Settings > Security.
func (s *Server) handleAuthLogoutEverywhere(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}

	sess := s.getSession()
	if sess == nil {
		writeErr(w, http.StatusUnauthorized, "not logged in")
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 10*time.Second)
	defer cancel()

	if err := cloud.Logout(ctx, s.cfg.SupabaseURL, s.cfg.SupabaseKey, sess.AccessToken, "global"); err != nil {
		writeErr(w, http.StatusBadGateway, err.Error())
		return
	}

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

// resolvePeriod turns a period keyword (+ optional from/to for
// "custom") into a half-open [start, end) unix-second range and a
// human label, all computed in the SERVER's local time zone (same
// zone the C agent's SQLite timestamps use) so "today" means the
// same thing here as it does in the tray app.
func resolvePeriod(period, fromStr, toStr string) (start, end time.Time, label string, singleDay bool, err error) {
	loc := time.Now().Location()
	now := time.Now()
	todayStart := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, loc)

	// Thứ Hai là đầu tuần (ISO 8601, khớp thói quen lịch phổ biến ở
	// VN) - Go's Weekday() có Sunday=0, cần quy đổi.
	weekday := int(todayStart.Weekday())
	if weekday == 0 {
		weekday = 7
	}
	mondayThisWeek := todayStart.AddDate(0, 0, -(weekday - 1))

	switch period {
	case "today", "":
		return todayStart, todayStart.Add(24 * time.Hour), "today", true, nil

	case "yesterday":
		y := todayStart.Add(-24 * time.Hour)
		return y, todayStart, "yesterday", true, nil

	case "this_week":
		return mondayThisWeek, mondayThisWeek.AddDate(0, 0, 7), "this_week", false, nil

	case "last_week":
		lastMonday := mondayThisWeek.AddDate(0, 0, -7)
		return lastMonday, mondayThisWeek, "last_week", false, nil

	case "custom":
		if fromStr == "" {
			return time.Time{}, time.Time{}, "", false, fmt.Errorf("from is required for period=custom")
		}
		if toStr == "" {
			toStr = fromStr
		}
		from, perr := time.ParseInLocation("2006-01-02", fromStr, loc)
		if perr != nil {
			return time.Time{}, time.Time{}, "", false, fmt.Errorf("from must be YYYY-MM-DD")
		}
		to, perr := time.ParseInLocation("2006-01-02", toStr, loc)
		if perr != nil {
			return time.Time{}, time.Time{}, "", false, fmt.Errorf("to must be YYYY-MM-DD")
		}
		if to.Before(from) {
			return time.Time{}, time.Time{}, "", false, fmt.Errorf("to must not be before from")
		}
		end := to.AddDate(0, 0, 1) // "to" is inclusive as a calendar day
		return from, end, "custom", from.Equal(to), nil

	default:
		return time.Time{}, time.Time{}, "", false, fmt.Errorf("unknown period %q", period)
	}
}

// handleLocalPeriod powers the history period picker (Today,
// Yesterday, This week, Last week, Custom) shared by the visual
// timeline and the usage-by-app table in the Local Activity tab.
func (s *Server) handleLocalPeriod(w http.ResponseWriter, r *http.Request) {
	period := r.URL.Query().Get("period")
	from := r.URL.Query().Get("from")
	to := r.URL.Query().Get("to")

	start, end, label, singleDay, err := resolvePeriod(period, from, to)
	if err != nil {
		writeErr(w, http.StatusBadRequest, err.Error())
		return
	}

	usage, err := s.db.UsageInRange(start.Unix(), end.Unix())
	if err != nil {
		writeErr(w, http.StatusServiceUnavailable, err.Error())
		return
	}

	resp := map[string]any{
		"period":     label,
		"from":       start.Format("2006-01-02"),
		"to":         end.AddDate(0, 0, -1).Format("2006-01-02"),
		"single_day": singleDay,
		"usage":      usage,
	}

	if singleDay {
		// Frontend hiển thị thanh timeline 24h qua /api/local/timeline
		// riêng - chỉ cần biết ngày để nó tự gọi.
		resp["date"] = start.Format("2006-01-02")
	} else {
		// Nhiều ngày: 1 thanh timeline 24h không còn hợp lý nữa - đổi
		// sang biểu đồ cột tổng theo từng ngày.
		dayBoundaries := make([]int64, 0)
		for d := start; !d.After(end); d = d.AddDate(0, 0, 1) {
			dayBoundaries = append(dayBoundaries, d.Unix())
		}

		daily, derr := s.db.DailyTotalsInRange(dayBoundaries)
		if derr != nil {
			writeErr(w, http.StatusServiceUnavailable, derr.Error())
			return
		}
		resp["daily_totals"] = daily
	}

	writeJSON(w, resp)
}

func (s *Server) handleLocalRecent(w http.ResponseWriter, r *http.Request) {
	recent, err := s.db.RecentActivities(50)
	if err != nil {
		writeErr(w, http.StatusServiceUnavailable, err.Error())
		return
	}
	writeJSON(w, map[string]any{"mode": "local", "recent": recent})
}

// timelineColors is a small fixed palette cycled deterministically per
// process name (same app always gets the same color within a single
// response, via a stable hash) - good enough for a handful of apps in
// one day without needing per-app config anywhere.
var timelineColors = []string{
	"#5aa9ff", "#22c55e", "#f5a524", "#a78bfa", "#f472b6",
	"#2dd4bf", "#fb923c", "#eab308", "#60a5fa", "#e879f9",
}

func timelineColorFor(name string) string {
	var h uint32
	for i := 0; i < len(name); i++ {
		h = h*31 + uint32(name[i])
	}
	return timelineColors[h%uint32(len(timelineColors))]
}

// handleTimeline powers the visual "Activity Timeline" widget: a
// single 24h strip with one colored segment per app-session, instead
// of the old plain scrollable list (which is still available via
// /api/local/recent for anyone who wants the raw feed). date is
// optional, "YYYY-MM-DD" in the dashboard machine's local time zone;
// defaults to today.
func (s *Server) handleTimeline(w http.ResponseWriter, r *http.Request) {
	loc := time.Now().Location()

	dayStart := time.Now()
	if dateStr := r.URL.Query().Get("date"); dateStr != "" {
		parsed, err := time.ParseInLocation("2006-01-02", dateStr, loc)
		if err != nil {
			writeErr(w, http.StatusBadRequest, "date must be YYYY-MM-DD")
			return
		}
		dayStart = parsed
	}

	start := time.Date(dayStart.Year(), dayStart.Month(), dayStart.Day(), 0, 0, 0, 0, loc)
	end := start.Add(24 * time.Hour)

	activities, err := s.db.ActivitiesForDay(start.Unix(), end.Unix())
	if err != nil {
		writeErr(w, http.StatusServiceUnavailable, err.Error())
		return
	}

	type segment struct {
		ProcessName string  `json:"process_name"`
		WindowTitle string  `json:"window_title"`
		StartTime   int64   `json:"start_time"`
		EndTime     int64   `json:"end_time"`
		Color       string  `json:"color"`
		StartPct    float64 `json:"start_pct"` // % qua ngày (0-100) - FE dùng để đặt vị trí/độ rộng
		WidthPct    float64 `json:"width_pct"`
	}

	dayLenSec := end.Sub(start).Seconds()
	segments := make([]segment, 0, len(activities))
	totalByApp := map[string]int64{}

	for _, a := range activities {
		if a.Duration <= 0 {
			continue
		}
		startPct := float64(a.StartTime-start.Unix()) / dayLenSec * 100
		widthPct := float64(a.Duration) / dayLenSec * 100
		if widthPct < 0.15 {
			widthPct = 0.15 // đoạn quá ngắn thì vẫn cho 1 vệt mảnh nhìn thấy được, thay vì biến mất
		}
		segments = append(segments, segment{
			ProcessName: a.ProcessName,
			WindowTitle: a.WindowTitle,
			StartTime:   a.StartTime,
			EndTime:     a.EndTime,
			Color:       timelineColorFor(a.ProcessName),
			StartPct:    startPct,
			WidthPct:    widthPct,
		})
		totalByApp[a.ProcessName] += a.Duration
	}

	type legendEntry struct {
		ProcessName  string `json:"process_name"`
		Color        string `json:"color"`
		TotalSeconds int64  `json:"total_seconds"`
	}
	legend := make([]legendEntry, 0, len(totalByApp))
	for name, secs := range totalByApp {
		legend = append(legend, legendEntry{ProcessName: name, Color: timelineColorFor(name), TotalSeconds: secs})
	}
	sort.Slice(legend, func(i, j int) bool { return legend[i].TotalSeconds > legend[j].TotalSeconds })

	writeJSON(w, map[string]any{
		"date":     start.Format("2006-01-02"),
		"segments": segments,
		"legend":   legend,
	})
}

type chatRequest struct {
	History []aiinsights.ChatMessage `json:"history"`
}

// handleChat is the small AI chatbot: it answers free-form questions
// about the person's OWN usage data (this device's local SQLite, last
// ~14 days) - "how much Discord did I use this week?" - by handing
// the conversation + a fresh usage snapshot to Gemini. Same opt-in,
// on-demand principle as handleInsights: only runs when the person
// sends a message, never automatically.
func (s *Server) handleChat(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}

	ds := dashsettings.Load()
	apiKey := ds.GeminiAPIKey
	if apiKey == "" {
		apiKey = os.Getenv("GEMINI_API_KEY")
	}
	if apiKey == "" {
		writeErr(w, http.StatusBadRequest, "Add your Gemini API key in Settings > AI Insights first.")
		return
	}

	var req chatRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || len(req.History) == 0 {
		writeErr(w, http.StatusBadRequest, "history (with at least one message) is required")
		return
	}
	// Giới hạn độ dài lịch sử gửi lên Gemini mỗi lần - tránh phình
	// request vô hạn khi cuộc trò chuyện dài dần theo thời gian.
	if len(req.History) > 20 {
		req.History = req.History[len(req.History)-20:]
	}

	usage, err := s.db.Usage(14)
	if err != nil {
		// Không có dữ liệu local vẫn cho chat tiếp - Chat() tự xử lý
		// usage rỗng bằng cách nói thẳng là chưa có dữ liệu.
		usage = nil
	}

	aiUsage := make([]aiinsights.AppUsage, 0, len(usage))
	for _, u := range usage {
		aiUsage = append(aiUsage, aiinsights.AppUsage{ProcessName: u.ProcessName, TotalSeconds: u.TotalSeconds})
	}

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	reply, err := aiinsights.Chat(ctx, apiKey, req.History, aiUsage)
	if err != nil {
		writeErr(w, http.StatusBadGateway, err.Error())
		return
	}

	writeJSON(w, map[string]any{"reply": reply})
}

// handleInsights is the small, on-demand "AI" feature: it sends the
// last ~7 days of app-usage totals — from local SQLite (this device
// only) or from Supabase (potentially every device on the account,
// pick via ?source=local|cloud) — to the Gemini API and returns a
// summary, a category per app, and a few supportive recommendations.
// This only runs when the person clicks the button; it is never
// called automatically or on a schedule.
func (s *Server) handleInsights(w http.ResponseWriter, r *http.Request) {
	ds := dashsettings.Load()

	apiKey := ds.GeminiAPIKey
	if apiKey == "" {
		apiKey = os.Getenv("GEMINI_API_KEY")
	}
	if apiKey == "" {
		writeErr(w, http.StatusBadRequest, "Add your Gemini API key in Settings > AI Insights first.")
		return
	}

	source := r.URL.Query().Get("source")
	if source == "" {
		source = "local"
	}

	var usage []aiinsights.AppUsage
	var sourceLabel string

	switch source {
	case "cloud":
		totals, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.DailyTotal, error) {
			from, to := dayRangeFor("week")
			return c.DailyTotals(r.Context(), from, to)
		})
		if err != nil {
			if errors.Is(err, errNotLoggedIn) {
				writeErr(w, http.StatusUnauthorized, "Log in to use cloud data for insights, or switch to Local.")
				return
			}
			s.writeCloudErr(w, err)
			return
		}

		byApp := map[string]int64{}
		for _, t := range totals {
			byApp[t.ProcessName] += t.TotalSeconds
		}
		for name, secs := range byApp {
			usage = append(usage, aiinsights.AppUsage{ProcessName: name, TotalSeconds: secs})
		}
		sourceLabel = "Supabase cloud sync — all of this account's devices, last 7 days"

	default: // "local"
		localUsage, err := s.db.Usage(7)
		if err != nil {
			writeErr(w, http.StatusServiceUnavailable, err.Error())
			return
		}
		for _, u := range localUsage {
			usage = append(usage, aiinsights.AppUsage{ProcessName: u.ProcessName, TotalSeconds: u.TotalSeconds})
		}
		sourceLabel = "local SQLite database — this device only, last 7 days"
	}

	if len(usage) == 0 {
		writeErr(w, http.StatusServiceUnavailable, "Not enough activity data yet for this source.")
		return
	}

	ctx, cancel := context.WithTimeout(r.Context(), 30*time.Second)
	defer cancel()

	insights, err := aiinsights.Generate(ctx, apiKey, usage, sourceLabel)
	if err != nil {
		writeErr(w, http.StatusBadGateway, err.Error())
		return
	}

	writeJSON(w, insights)
}

// ---------------------------------------------------------------------
// Machines (multi-device overview). Every device running this
// dashboard while logged in pushes its own heartbeat every 30s (see
// heartbeatLoop) — this just reads them all back for the account.
// This is also literally "the connection between 2 machines": once
// both are logged into the same account, each shows up in the
// other's Machines list automatically, with no extra setup.
// ---------------------------------------------------------------------

func (s *Server) handleMachines(w http.ResponseWriter, r *http.Request) {
	heartbeats, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.DeviceHeartbeat, error) {
		return c.ListHeartbeats(r.Context())
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeJSON(w, map[string]any{"machines": []cloud.DeviceHeartbeat{}, "logged_in": false, "self_device_id": s.cfg.DeviceID})
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"machines": heartbeats, "logged_in": true, "self_device_id": s.cfg.DeviceID})
}

type removeMachineRequest struct {
	DeviceID string `json:"device_id"`
}

// handleMachineRemove lets the person delete a stale device_heartbeats
// row - most importantly, a LEGACY row created before the device-id
// anonymization fix, whose hostname/device_id fields still hold the
// real Windows computer name (see machines-list rendering in
// dashboard.js, which now refuses to display anything that isn't in
// the anonymized "PC-XXXXXXXX" format and offers this Remove action
// instead). RLS restricts the delete to rows this account owns.
func (s *Server) handleMachineRemove(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req removeMachineRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.DeviceID == "" {
		writeErr(w, http.StatusBadRequest, "device_id is required")
		return
	}

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.DeleteHeartbeat(r.Context(), req.DeviceID)
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeErr(w, http.StatusUnauthorized, "not logged in")
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

// ---------------------------------------------------------------------
// Device-to-device messaging API — has no Python equivalent. Only
// ever relayed through Supabase; never a direct connection.
// ---------------------------------------------------------------------

func (s *Server) handleDevices(w http.ResponseWriter, r *http.Request) {
	/*
	 * BUG CŨ ("Machine chỉ hiển thị các máy đã kết bạn qua email" -
	 * thực ra là: danh sách chip chọn máy để chat chỉ hiện ÍT máy
	 * hơn hẳn so với mục "Machines"): trước đây lấy danh sách máy từ
	 * DailyTotals (activity_daily_totals - CHỈ có mặt khi máy đó đã
	 * ĐỒNG BỘ ít nhất 1 bản ghi hoạt động lên cloud), trong khi mục
	 * "Machines" lấy từ device_heartbeats (có mặt ngay khi máy gửi
	 * heartbeat đầu tiên - giờ agent C tự làm việc này, xem
	 * machines.c). Kết quả: máy mới cài, hoặc máy ít hoạt động/chưa
	 * tới lúc đồng bộ, xuất hiện ở "Machines" nhưng KHÔNG xuất hiện
	 * trong danh sách chọn để chat - gây khó hiểu. Giờ dùng CHUNG 1
	 * nguồn (device_heartbeats) cho cả 2 nơi.
	 */
	heartbeats, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.DeviceHeartbeat, error) {
		return c.ListHeartbeats(r.Context())
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
	for _, hb := range heartbeats {
		if hb.DeviceID != s.cfg.DeviceID {
			others = append(others, hb.DeviceID)
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

// handleThread is the real two-way "chat with this device" view (see
// cloud.Client.Thread) — still 100% relayed through Supabase, exactly
// like everything else here; it never opens a direct connection to
// the other machine, so it works the same way even if the other
// device is offline right now (the messages just wait in the table
// until it next opens the dashboard).
func (s *Server) handleThread(w http.ResponseWriter, r *http.Request) {
	otherID := r.URL.Query().Get("device_id")
	if otherID == "" {
		writeErr(w, http.StatusBadRequest, "device_id is required")
		return
	}

	msgs, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.Message, error) {
		return c.Thread(r.Context(), s.cfg.DeviceID, otherID, 100)
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeJSON(w, map[string]any{"messages": []cloud.Message{}, "logged_in": false})
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"messages": msgs, "self_device_id": s.cfg.DeviceID, "logged_in": true})
}

type shareStatsRequest struct {
	TargetDeviceID string `json:"target_device_id"`
}

// handleShareStats answers a "what are your stats right now" request
// from another of the user's own devices: it packages THIS device's
// current live CPU/RAM/disk (same numbers the Overview cards show)
// into a kind="data" message and relays it through Supabase, same as
// any other device_messages row - the requester picks it up next time
// they poll their thread (loadDeviceThread()), no direct connection.
func (s *Server) handleShareStats(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req shareStatsRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.TargetDeviceID == "" {
		writeErr(w, http.StatusBadRequest, "target_device_id is required")
		return
	}

	live := s.collector.Live()
	payload, _ := json.Marshal(map[string]any{
		"type":         "stats_reply",
		"label":        s.cfg.DisplayLabel(),
		"cpu_percent":  live.CPUPercent,
		"ram_percent":  live.RAMPercent,
		"disk_percent": live.DiskPercent,
	})

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.SendMessage(r.Context(), cloud.Message{
			SenderDeviceID: s.cfg.DeviceID,
			TargetDeviceID: req.TargetDeviceID,
			Kind:           "data",
			Payload:        string(payload),
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
// Parent/child linking + app limits API (see
// migrations/004_parent_links.sql). Consent-based: a parent invites by
// email, but sees nothing until the child approves, and the child can
// revoke at any time. Mirrors the native Qt ParentDialog/
// ParentLinkDialog in the C++ agent — same backend, same rules, either
// UI works interchangeably for the same account.
// ---------------------------------------------------------------------

func (s *Server) handleParentChildren(w http.ResponseWriter, r *http.Request) {
	links, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.ParentLink, error) {
		return c.ListLinksAsParent(r.Context())
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeJSON(w, map[string]any{"links": []cloud.ParentLink{}, "logged_in": false})
			return
		}
		s.writeCloudErr(w, err)
		return
	}

	// RBAC: gộp permission_level vào từng link (xem
	// migrations/005_permission_levels.sql). Lỗi ở bước này không nên
	// chặn cả trang - nếu không lấy được, coi như "full" (giá trị mặc
	// định của migration) để không đột ngột khoá quyền ai cả.
	perms, permErr := withAuthRetry(s, r.Context(), func(c *cloud.Client) (map[int64]string, error) {
		return c.ListPermissions(r.Context())
	})

	type childRow struct {
		cloud.ParentLink
		PermissionLevel string `json:"permission_level"`
	}

	rows := make([]childRow, 0, len(links))
	for _, l := range links {
		level := "full"
		if permErr == nil {
			if v, ok := perms[l.ID]; ok && v != "" {
				level = v
			}
		}
		rows = append(rows, childRow{ParentLink: l, PermissionLevel: level})
	}

	writeJSON(w, map[string]any{"links": rows, "logged_in": true})
}

type setPermissionRequest struct {
	ID              int64  `json:"id"`
	PermissionLevel string `json:"permission_level"`
}

func (s *Server) handleParentSetPermission(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req setPermissionRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.ID == 0 {
		writeErr(w, http.StatusBadRequest, "id and permission_level are required")
		return
	}
	if req.PermissionLevel != "full" && req.PermissionLevel != "view_only" {
		writeErr(w, http.StatusBadRequest, "permission_level must be 'full' or 'view_only'")
		return
	}

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.SetPermission(r.Context(), req.ID, req.PermissionLevel)
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeErr(w, http.StatusUnauthorized, "not logged in")
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

// permissionForChild looks up this parent's permission_level for a
// given child_user_id - "full" if not logged in to a working answer
// isn't possible, or if the link isn't found (defensive default that
// matches the migration's own column default, never silently grants
// MORE than that).
func (s *Server) permissionForChild(ctx context.Context, childUserID string) string {
	links, err := withAuthRetry(s, ctx, func(c *cloud.Client) ([]cloud.ParentLink, error) {
		return c.ListLinksAsParent(ctx)
	})
	if err != nil {
		return "full"
	}

	var linkID int64 = -1
	for _, l := range links {
		if l.OtherUserID == childUserID {
			linkID = l.ID
			break
		}
	}
	if linkID < 0 {
		return "full"
	}

	perms, err := withAuthRetry(s, ctx, func(c *cloud.Client) (map[int64]string, error) {
		return c.ListPermissions(ctx)
	})
	if err != nil {
		return "full"
	}
	if v, ok := perms[linkID]; ok && v != "" {
		return v
	}
	return "full"
}

func (s *Server) handleParentParents(w http.ResponseWriter, r *http.Request) {
	links, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.ParentLink, error) {
		return c.ListLinksAsChild(r.Context())
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeJSON(w, map[string]any{"links": []cloud.ParentLink{}, "logged_in": false})
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"links": links, "logged_in": true})
}

type inviteRequest struct {
	ChildEmail string `json:"child_email"`
}

func (s *Server) handleParentInvite(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req inviteRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil || req.ChildEmail == "" {
		writeErr(w, http.StatusBadRequest, "child_email is required")
		return
	}

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.InviteChild(r.Context(), req.ChildEmail)
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeErr(w, http.StatusUnauthorized, "not logged in")
			return
		}
		writeErr(w, http.StatusBadGateway, err.Error())
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

type linkIDRequest struct {
	ID int64 `json:"id"`
}

func (s *Server) handleParentApprove(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req linkIDRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErr(w, http.StatusBadRequest, "invalid request body")
		return
	}

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.ApproveLink(r.Context(), req.ID)
	})
	if err != nil {
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

func (s *Server) handleParentRevoke(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req linkIDRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErr(w, http.StatusBadRequest, "invalid request body")
		return
	}

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.RevokeLink(r.Context(), req.ID)
	})
	if err != nil {
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

func (s *Server) handleParentChildSummary(w http.ResponseWriter, r *http.Request) {
	childID := r.URL.Query().Get("child_id")
	if childID == "" {
		writeErr(w, http.StatusBadRequest, "child_id is required")
		return
	}

	// Hôm nay theo giờ địa phương của máy chạy dashboard (đơn
	// giản, khớp với cách agent C tính "hôm nay" cục bộ).
	now := time.Now()
	todayStart := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, now.Location())

	logs, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.RecentLog, error) {
		return c.ActivityLogsForChild(r.Context(), childID, todayStart.Unix(), 500)
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeErr(w, http.StatusUnauthorized, "not logged in")
			return
		}
		s.writeCloudErr(w, err)
		return
	}

	byApp := map[string]int64{}
	for _, l := range logs {
		byApp[l.ProcessName] += l.Duration
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

	writeJSON(w, map[string]any{"apps": apps, "recent": logs})
}

func (s *Server) handleParentChildHeartbeat(w http.ResponseWriter, r *http.Request) {
	childID := r.URL.Query().Get("child_id")
	if childID == "" {
		writeErr(w, http.StatusBadRequest, "child_id is required")
		return
	}

	heartbeats, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.DeviceHeartbeat, error) {
		return c.ListHeartbeatsForChild(r.Context(), childID)
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeErr(w, http.StatusUnauthorized, "not logged in")
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"machines": heartbeats})
}

func (s *Server) handleParentLimits(w http.ResponseWriter, r *http.Request) {
	childID := r.URL.Query().Get("child_id")
	if childID == "" {
		writeErr(w, http.StatusBadRequest, "child_id is required")
		return
	}

	limits, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) ([]cloud.AppLimit, error) {
		return c.ListLimitsForChild(r.Context(), childID)
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeErr(w, http.StatusUnauthorized, "not logged in")
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"limits": limits})
}

type setLimitRequest struct {
	ChildUserID   string `json:"child_user_id"`
	ProcessName   string `json:"process_name"`
	DailyLimitMin *int   `json:"daily_limit_min"` // phút; null = không giới hạn
	Blocked       bool   `json:"blocked"`
}

func (s *Server) handleParentLimitSet(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req setLimitRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErr(w, http.StatusBadRequest, "invalid request body")
		return
	}
	if req.ChildUserID == "" || req.ProcessName == "" {
		writeErr(w, http.StatusBadRequest, "child_user_id and process_name are required")
		return
	}

	var dailyLimitSec *int
	if req.DailyLimitMin != nil {
		sec := *req.DailyLimitMin * 60
		dailyLimitSec = &sec
	}

	// RBAC (xem migrations/005_permission_levels.sql): phụ huynh với
	// quyền "view_only" chỉ được XEM, không được đặt giới hạn app.
	if s.permissionForChild(r.Context(), req.ChildUserID) != "full" {
		writeErr(w, http.StatusForbidden, "Bạn chỉ có quyền xem (view-only) với tài khoản này, không thể đặt giới hạn app.")
		return
	}

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.SetLimit(r.Context(), req.ChildUserID, req.ProcessName, dailyLimitSec, req.Blocked)
	})
	if err != nil {
		if errors.Is(err, errNotLoggedIn) {
			writeErr(w, http.StatusUnauthorized, "not logged in")
			return
		}
		s.writeCloudErr(w, err)
		return
	}
	writeJSON(w, map[string]any{"ok": true})
}

type deleteLimitRequest struct {
	ID          int64  `json:"id"`
	ChildUserID string `json:"child_user_id"`
}

func (s *Server) handleParentLimitDelete(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		writeErr(w, http.StatusMethodNotAllowed, "POST only")
		return
	}
	var req deleteLimitRequest
	if err := json.NewDecoder(r.Body).Decode(&req); err != nil {
		writeErr(w, http.StatusBadRequest, "invalid request body")
		return
	}

	// RBAC: cùng luật với handleParentLimitSet ở trên. child_user_id
	// do frontend gửi kèm (nó đã biết đang xoá giới hạn của con nào -
	// xem dashboard.js deleteLimit()).
	if req.ChildUserID != "" && s.permissionForChild(r.Context(), req.ChildUserID) != "full" {
		writeErr(w, http.StatusForbidden, "Bạn chỉ có quyền xem (view-only) với tài khoản này, không thể xoá giới hạn app.")
		return
	}

	_, err := withAuthRetry(s, r.Context(), func(c *cloud.Client) (struct{}, error) {
		return struct{}{}, c.DeleteLimit(r.Context(), req.ID)
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

	case "ai_key":
		ds := dashsettings.Load()
		newKey := strings.TrimSpace(r.FormValue("gemini_api_key"))
		clear := r.FormValue("clear_gemini_key") == "1"

		if clear {
			ds.GeminiAPIKey = ""
		} else if newKey != "" {
			// Bỏ trống ô input + bấm Save = GIỮ NGUYÊN key cũ (xem
			// settings.html - ô input không còn hiện lại key thật
			// nữa, nên "trống" không có nghĩa là "người dùng muốn
			// xoá", chỉ checkbox "clear_gemini_key" mới xoá thật).
			ds.GeminiAPIKey = newKey
		}

		_ = dashsettings.Save(ds)
		data = s.basePageData("settings")
		data.Extra["message"] = data.T["save_success"]
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
