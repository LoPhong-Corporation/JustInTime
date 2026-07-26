// Package cloud is a Go port of supabase_client.py: it logs into the
// same Supabase project as the C agent and the previous dashboard,
// fetches activity_daily_totals / activity_logs (scoped to the caller
// by Row Level Security), and adds a new capability the Python version
// never had — relaying small messages/data between the user's own
// devices by device_id (see migrations/002_device_messages.sql).
package cloud

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"sort"
	"time"
)

// ErrUnauthorized marks a 401 from Supabase REST, so callers can try a
// token refresh once and retry (see server.withAuthRetry).
var ErrUnauthorized = errors.New("unauthorized")

type AuthError struct {
	Message string
}

func (e *AuthError) Error() string { return e.Message }

type Client struct {
	BaseURL     string
	ApiKey      string
	AccessToken string
	HTTP        *http.Client
}

func New(baseURL, apiKey, accessToken string) *Client {
	return &Client{
		BaseURL:     baseURL,
		ApiKey:      apiKey,
		AccessToken: accessToken,
		HTTP:        &http.Client{Timeout: 15 * time.Second},
	}
}

// Session mirrors the dict returned by supabase_client.login()/refresh_session().
type Session struct {
	AccessToken  string `json:"access_token"`
	RefreshToken string `json:"refresh_token"`
	UserID       string `json:"user_id"`
	Email        string `json:"email"`
}

func decodeAuthError(resp *http.Response) error {
	var data map[string]any
	body, _ := io.ReadAll(resp.Body)
	_ = json.Unmarshal(body, &data)

	msg := fmt.Sprintf("unexpected error (HTTP %d)", resp.StatusCode)
	for _, key := range []string{"error_description", "msg", "message"} {
		if v, ok := data[key].(string); ok && v != "" {
			msg = v
			break
		}
	}
	return &AuthError{Message: msg}
}

// Login performs an email+password grant against Supabase Auth (GoTrue),
// same as supabase_client.login().
func Login(ctx context.Context, baseURL, apiKey, email, password string) (*Session, error) {
	body, _ := json.Marshal(map[string]string{"email": email, "password": password})

	req, err := http.NewRequestWithContext(ctx, http.MethodPost,
		baseURL+"/auth/v1/token?grant_type=password", bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	req.Header.Set("apikey", apiKey)
	req.Header.Set("Content-Type", "application/json")

	resp, err := (&http.Client{Timeout: 15 * time.Second}).Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, decodeAuthError(resp)
	}
	return parseSessionResponse(resp.Body)
}

// RefreshSession exchanges a refresh_token for a new access_token, same
// as supabase_client.refresh_session().
func RefreshSession(ctx context.Context, baseURL, apiKey, refreshToken string) (*Session, error) {
	body, _ := json.Marshal(map[string]string{"refresh_token": refreshToken})

	req, err := http.NewRequestWithContext(ctx, http.MethodPost,
		baseURL+"/auth/v1/token?grant_type=refresh_token", bytes.NewReader(body))
	if err != nil {
		return nil, err
	}
	req.Header.Set("apikey", apiKey)
	req.Header.Set("Content-Type", "application/json")

	resp, err := (&http.Client{Timeout: 15 * time.Second}).Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, decodeAuthError(resp)
	}
	return parseSessionResponse(resp.Body)
}

func parseSessionResponse(r io.Reader) (*Session, error) {
	var raw struct {
		AccessToken  string `json:"access_token"`
		RefreshToken string `json:"refresh_token"`
		User         struct {
			ID    string `json:"id"`
			Email string `json:"email"`
		} `json:"user"`
	}
	if err := json.NewDecoder(r).Decode(&raw); err != nil {
		return nil, err
	}
	return &Session{
		AccessToken:  raw.AccessToken,
		RefreshToken: raw.RefreshToken,
		UserID:       raw.User.ID,
		Email:        raw.User.Email,
	}, nil
}

// ChangePassword updates the logged-in user's password, same as
// supabase_client.change_password().
func (c *Client) ChangePassword(ctx context.Context, newPassword string) error {
	body, _ := json.Marshal(map[string]string{"password": newPassword})

	resp, err := c.request(ctx, http.MethodPut, "/auth/v1/user", body, nil)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return decodeAuthError(resp)
	}
	return nil
}

func (c *Client) request(ctx context.Context, method, path string, body []byte, extraHeaders map[string]string) (*http.Response, error) {
	var reader io.Reader
	if body != nil {
		reader = bytes.NewReader(body)
	}

	req, err := http.NewRequestWithContext(ctx, method, c.BaseURL+path, reader)
	if err != nil {
		return nil, err
	}
	req.Header.Set("apikey", c.ApiKey)
	req.Header.Set("Authorization", "Bearer "+c.AccessToken)
	req.Header.Set("Content-Type", "application/json")
	for k, v := range extraHeaders {
		req.Header.Set(k, v)
	}
	return c.HTTP.Do(req)
}

func readErr(resp *http.Response) error {
	b, _ := io.ReadAll(resp.Body)
	if resp.StatusCode == http.StatusUnauthorized {
		return fmt.Errorf("%w: %s", ErrUnauthorized, string(b))
	}
	return fmt.Errorf("supabase returned %d: %s", resp.StatusCode, string(b))
}

// DailyTotal is a row from the activity_daily_totals view.
type DailyTotal struct {
	DeviceID     string `json:"device_id"`
	ProcessName  string `json:"process_name"`
	Day          string `json:"day"`
	TotalSeconds int64  `json:"total_seconds"`
}

// DailyTotals is a port of supabase_client.fetch_daily_totals(): reads
// the view for the logged-in user (RLS-scoped), optionally bounded to
// [dayFrom, dayTo] ("YYYY-MM-DD").
func (c *Client) DailyTotals(ctx context.Context, dayFrom, dayTo string) ([]DailyTotal, error) {
	q := url.Values{}
	q.Set("select", "*")
	q.Set("order", "day.desc,total_seconds.desc")
	q.Set("limit", "1000")

	switch {
	case dayFrom != "" && dayTo != "":
		q.Set("and", fmt.Sprintf("(day.gte.%s,day.lte.%s)", dayFrom, dayTo))
	case dayFrom != "":
		q.Set("day", "gte."+dayFrom)
	case dayTo != "":
		q.Set("day", "lte."+dayTo)
	}

	resp, err := c.request(ctx, http.MethodGet, "/rest/v1/activity_daily_totals?"+q.Encode(), nil, nil)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, readErr(resp)
	}

	var out []DailyTotal
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nil, err
	}
	return out, nil
}

// RecentLog is a row from activity_logs (subset of columns, matching
// supabase_client.fetch_recent_logs()).
type RecentLog struct {
	DeviceID    string `json:"device_id"`
	ProcessName string `json:"process_name"`
	WindowTitle string `json:"window_title"`
	Duration    int64  `json:"duration_seconds"`
	StartTime   int64  `json:"start_time"`
	EndTime     int64  `json:"end_time"`
}

func (c *Client) RecentLogs(ctx context.Context, limit int) ([]RecentLog, error) {
	q := url.Values{}
	q.Set("select", "device_id,process_name,window_title,duration_seconds,start_time,end_time")
	q.Set("order", "start_time.desc")
	q.Set("limit", fmt.Sprintf("%d", limit))

	resp, err := c.request(ctx, http.MethodGet, "/rest/v1/activity_logs?"+q.Encode(), nil, nil)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, readErr(resp)
	}

	var out []RecentLog
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nil, err
	}
	return out, nil
}

// Ping is a cheap reachability + token-validity check, used by the
// server to decide whether to show cloud data or fall back to local.
func (c *Client) Ping(ctx context.Context) error {
	resp, err := c.request(ctx, http.MethodGet, "/rest/v1/activity_daily_totals?limit=1", nil, nil)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode >= 400 {
		return readErr(resp)
	}
	return nil
}

// Devices extracts the distinct device_id list from already-fetched
// totals (PostgREST has no cheap server-side DISTINCT without a
// dedicated view, and this is cheap enough at this data size).
func Devices(totals []DailyTotal) []string {
	seen := map[string]bool{}
	var out []string
	for _, t := range totals {
		if !seen[t.DeviceID] {
			seen[t.DeviceID] = true
			out = append(out, t.DeviceID)
		}
	}
	sort.Strings(out)
	return out
}

// Message is a row of public.device_messages (see
// migrations/002_device_messages.sql). user_id is intentionally
// omitted: the column defaults to auth.uid() server-side.
type Message struct {
	ID             int64   `json:"id,omitempty"`
	SenderDeviceID string  `json:"sender_device_id"`
	TargetDeviceID string  `json:"target_device_id"`
	Kind           string  `json:"kind"` // "message" or "data"
	Payload        string  `json:"payload"`
	CreatedAt      string  `json:"created_at,omitempty"`
	ReadAt         *string `json:"read_at,omitempty"`
}

// SendMessage relays a message or data payload to another of the user's
// own devices, addressed only by device_id. The client never opens a
// direct connection to the other machine — everything is relayed
// through Supabase, exactly like activity sync already is.
func (c *Client) SendMessage(ctx context.Context, msg Message) error {
	body, err := json.Marshal(msg)
	if err != nil {
		return err
	}

	resp, err := c.request(ctx, http.MethodPost, "/rest/v1/device_messages", body, map[string]string{
		"Prefer": "return=minimal",
	})
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 400 {
		return readErr(resp)
	}
	return nil
}

// Inbox returns messages/data addressed to selfDeviceID, newest first.
func (c *Client) Inbox(ctx context.Context, selfDeviceID string, limit int) ([]Message, error) {
	path := fmt.Sprintf(
		"/rest/v1/device_messages?target_device_id=eq.%s&order=created_at.desc&limit=%d",
		url.QueryEscape(selfDeviceID), limit,
	)

	resp, err := c.request(ctx, http.MethodGet, path, nil, nil)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 400 {
		return nil, readErr(resp)
	}

	var out []Message
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nil, err
	}
	return out, nil
}

// MarkRead stamps read_at once the user has seen a message.
func (c *Client) MarkRead(ctx context.Context, id int64) error {
	body, _ := json.Marshal(map[string]string{
		"read_at": time.Now().UTC().Format(time.RFC3339),
	})

	resp, err := c.request(ctx, http.MethodPatch,
		fmt.Sprintf("/rest/v1/device_messages?id=eq.%d", id),
		body, map[string]string{"Prefer": "return=minimal"})
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 400 {
		return readErr(resp)
	}
	return nil
}

// ---------------------------------------------------------------------
// Parent/child linking (see migrations/004_parent_links.sql). This is a
// consent-based model: a parent invites a child by email, but nothing
// is visible to the parent until the CHILD explicitly approves — and
// the child can revoke at any time. The Go dashboard is the primary
// place both roles manage this (mirrors the native Qt ParentDialog/
// ParentLinkDialog in the C++ agent, same backend, same rules).
// ---------------------------------------------------------------------

// ParentLink is a normalized view of a parent_links row, whichever side
// it was fetched from (see ListLinksAsParent/ListLinksAsChild) —
// OtherUserID/OtherEmail is the *other* party in the link.
type ParentLink struct {
	ID          int64   `json:"id"`
	OtherUserID string  `json:"other_user_id"`
	OtherEmail  string  `json:"other_email"`
	Status      string  `json:"status"` // "pending" | "approved" | "revoked"
	CreatedAt   string  `json:"created_at"`
	ApprovedAt  *string `json:"approved_at"`
}

type parentLinkAsParentRow struct {
	ID          int64   `json:"id"`
	ChildUserID string  `json:"child_user_id"`
	ChildEmail  string  `json:"child_email"`
	Status      string  `json:"status"`
	CreatedAt   string  `json:"created_at"`
	ApprovedAt  *string `json:"approved_at"`
}

type parentLinkAsChildRow struct {
	ID           int64   `json:"id"`
	ParentUserID string  `json:"parent_user_id"`
	ParentEmail  string  `json:"parent_email"`
	Status       string  `json:"status"`
	CreatedAt    string  `json:"created_at"`
	ApprovedAt   *string `json:"approved_at"`
}

// InviteChild sends a monitoring invite to child_email. The invite sits
// as "pending" until the child account approves it — nothing about the
// child is readable by the parent before that (enforced by RLS, not
// just by this client).
func (c *Client) InviteChild(ctx context.Context, childEmail string) error {
	lookupBody, _ := json.Marshal(map[string]string{"target_email": childEmail})

	resp, err := c.request(ctx, http.MethodPost, "/rest/v1/rpc/find_user_id_by_email", lookupBody, nil)
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return readErr(resp)
	}

	var childID *string
	if err := json.NewDecoder(resp.Body).Decode(&childID); err != nil {
		return err
	}
	if childID == nil || *childID == "" {
		return fmt.Errorf("no JustInTime account found for %q — they need to sign up/log in with this exact email first", childEmail)
	}

	insertBody, _ := json.Marshal(map[string]string{"child_user_id": *childID})

	resp2, err := c.request(ctx, http.MethodPost, "/rest/v1/parent_links", insertBody, map[string]string{
		"Prefer": "return=minimal",
	})
	if err != nil {
		return err
	}
	defer resp2.Body.Close()

	if resp2.StatusCode >= 200 && resp2.StatusCode < 300 {
		return nil
	}
	if resp2.StatusCode == http.StatusConflict {
		return fmt.Errorf("already invited or linked with this account")
	}
	return readErr(resp2)
}

// ListLinksAsParent lists every child link this account has sent (any
// status), via the parent_links_for_parent() RPC (joins email server-
// side so the client never touches auth.users directly).
func (c *Client) ListLinksAsParent(ctx context.Context) ([]ParentLink, error) {
	resp, err := c.request(ctx, http.MethodPost, "/rest/v1/rpc/parent_links_for_parent", []byte("{}"), nil)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, readErr(resp)
	}

	var raw []parentLinkAsParentRow
	if err := json.NewDecoder(resp.Body).Decode(&raw); err != nil {
		return nil, err
	}

	out := make([]ParentLink, len(raw))
	for i, r := range raw {
		out[i] = ParentLink{
			ID: r.ID, OtherUserID: r.ChildUserID, OtherEmail: r.ChildEmail,
			Status: r.Status, CreatedAt: r.CreatedAt, ApprovedAt: r.ApprovedAt,
		}
	}
	return out, nil
}

// ListLinksAsChild lists every parent who has invited or been approved
// to view this account (any status) — the transparency view: nothing
// is ever hidden, including pending invites the child hasn't acted on.
func (c *Client) ListLinksAsChild(ctx context.Context) ([]ParentLink, error) {
	resp, err := c.request(ctx, http.MethodPost, "/rest/v1/rpc/parent_links_for_child", []byte("{}"), nil)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, readErr(resp)
	}

	var raw []parentLinkAsChildRow
	if err := json.NewDecoder(resp.Body).Decode(&raw); err != nil {
		return nil, err
	}

	out := make([]ParentLink, len(raw))
	for i, r := range raw {
		out[i] = ParentLink{
			ID: r.ID, OtherUserID: r.ParentUserID, OtherEmail: r.ParentEmail,
			Status: r.Status, CreatedAt: r.CreatedAt, ApprovedAt: r.ApprovedAt,
		}
	}
	return out, nil
}

func (c *Client) updateLinkStatus(ctx context.Context, linkID int64, status string) error {
	body, _ := json.Marshal(map[string]string{"status": status})

	resp, err := c.request(ctx, http.MethodPatch,
		fmt.Sprintf("/rest/v1/parent_links?id=eq.%d", linkID),
		body, map[string]string{"Prefer": "return=minimal"})
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 200 && resp.StatusCode < 300 {
		return nil
	}
	return readErr(resp)
}

// ApproveLink is a child-side action: consent to let a parent view this
// account's activity.
func (c *Client) ApproveLink(ctx context.Context, linkID int64) error {
	return c.updateLinkStatus(ctx, linkID, "approved")
}

// RevokeLink works from either side: a child stops sharing with a
// parent, or a parent cancels an invite they sent.
func (c *Client) RevokeLink(ctx context.Context, linkID int64) error {
	return c.updateLinkStatus(ctx, linkID, "revoked")
}

// ---------------------------------------------------------------------
// App limits (see migrations/004_parent_links.sql). Only ever readable/
// writable by a parent with an "approved" link to that child — RLS
// enforces this regardless of what this client sends.
// ---------------------------------------------------------------------

// AppLimit is a row of public.app_limits. DailyLimitSec is nil for "no
// time limit" (only meaningful when Blocked is also false).
type AppLimit struct {
	ID            int64  `json:"id"`
	ProcessName   string `json:"process_name"`
	DailyLimitSec *int   `json:"daily_limit_sec"`
	Blocked       bool   `json:"blocked"`
}

// ListLimitsForChild returns every limit set for childUserID (only
// succeeds if the caller is a parent approved for that child).
func (c *Client) ListLimitsForChild(ctx context.Context, childUserID string) ([]AppLimit, error) {
	path := fmt.Sprintf(
		"/rest/v1/app_limits?select=id,process_name,daily_limit_sec,blocked&child_user_id=eq.%s&order=process_name.asc",
		url.QueryEscape(childUserID),
	)

	resp, err := c.request(ctx, http.MethodGet, path, nil, nil)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, readErr(resp)
	}

	var out []AppLimit
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nil, err
	}
	return out, nil
}

// SetLimit upserts a limit for 1 app (unique on child_user_id +
// process_name — calling again for the same app overwrites it).
// dailyLimitSec == nil means "no time limit".
func (c *Client) SetLimit(ctx context.Context, childUserID, processName string, dailyLimitSec *int, blocked bool) error {
	payload := map[string]any{
		"child_user_id":   childUserID,
		"process_name":    processName,
		"daily_limit_sec": nil,
		"blocked":         blocked,
	}
	if dailyLimitSec != nil {
		payload["daily_limit_sec"] = *dailyLimitSec
	}

	body, err := json.Marshal(payload)
	if err != nil {
		return err
	}

	resp, err := c.request(ctx, http.MethodPost,
		"/rest/v1/app_limits?on_conflict=child_user_id,process_name",
		body, map[string]string{"Prefer": "resolution=merge-duplicates,return=minimal"})
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 200 && resp.StatusCode < 300 {
		return nil
	}
	return readErr(resp)
}

// DeleteLimit removes a limit entirely (the app becomes unrestricted).
func (c *Client) DeleteLimit(ctx context.Context, limitID int64) error {
	resp, err := c.request(ctx, http.MethodDelete,
		fmt.Sprintf("/rest/v1/app_limits?id=eq.%d", limitID),
		nil, map[string]string{"Prefer": "return=minimal"})
	if err != nil {
		return err
	}
	defer resp.Body.Close()

	if resp.StatusCode >= 200 && resp.StatusCode < 300 {
		return nil
	}
	return readErr(resp)
}

// ActivityLogsForChild fetches raw activity_logs rows for a specific
// child (only succeeds for a parent approved for that child — RLS,
// see migrations/004_parent_links.sql). sinceUnix=0 means no lower
// bound on start_time.
func (c *Client) ActivityLogsForChild(ctx context.Context, childUserID string, sinceUnix int64, limit int) ([]RecentLog, error) {
	q := url.Values{}
	q.Set("select", "device_id,process_name,window_title,duration_seconds,start_time,end_time")
	q.Set("user_id", "eq."+childUserID)
	if sinceUnix > 0 {
		q.Set("start_time", fmt.Sprintf("gte.%d", sinceUnix))
	}
	q.Set("order", "start_time.desc")
	q.Set("limit", fmt.Sprintf("%d", limit))

	resp, err := c.request(ctx, http.MethodGet, "/rest/v1/activity_logs?"+q.Encode(), nil, nil)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	if resp.StatusCode != http.StatusOK {
		return nil, readErr(resp)
	}

	var out []RecentLog
	if err := json.NewDecoder(resp.Body).Decode(&out); err != nil {
		return nil, err
	}
	return out, nil
}
