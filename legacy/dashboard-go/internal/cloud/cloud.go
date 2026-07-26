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
