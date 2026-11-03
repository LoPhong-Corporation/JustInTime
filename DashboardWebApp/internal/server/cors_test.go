package server

import (
	"net/http"
	"net/http/httptest"
	"testing"
)

func TestAllowedDashboardOrigins(t *testing.T) {
	tests := []struct {
		origin string
		want   bool
	}{
		{"http://127.0.0.1:5174", true},
		{"http://localhost:1420", true},
		{"http://tauri.localhost", true},
		{"https://tauri.localhost", true},
		{"tauri://localhost", true},
		{"http://127.0.0.1:5000", true},
		{"http://example.com:5174", false},
		{"http://localhost:5174.evil.test", false},
		{"http://localhost:5174/path", false},
	}

	for _, test := range tests {
		t.Run(test.origin, func(t *testing.T) {
			if got := isAllowedDashboardOrigin(test.origin, 5000); got != test.want {
				t.Fatalf("isAllowedDashboardOrigin(%q) = %t, want %t", test.origin, got, test.want)
			}
		})
	}
}

func TestAllowedDashboardOriginIncludesConfiguredPort(t *testing.T) {
	if !isAllowedDashboardOrigin("http://localhost:5123", 5123) {
		t.Fatal("configured Go dashboard origin should be allowed")
	}
}

func TestServeHTTPHandlesAllowedCORSPreflight(t *testing.T) {
	mux := http.NewServeMux()
	mux.HandleFunc("/api/test", func(w http.ResponseWriter, _ *http.Request) {
		w.WriteHeader(http.StatusOK)
	})
	srv := &Server{mux: mux}

	req := httptest.NewRequest(http.MethodOptions, "/api/test", nil)
	req.Header.Set("Origin", "http://127.0.0.1:5174")
	req.Header.Set("Access-Control-Request-Method", "POST")
	res := httptest.NewRecorder()
	srv.ServeHTTP(res, req)

	if res.Code != http.StatusNoContent {
		t.Fatalf("preflight status = %d, want %d", res.Code, http.StatusNoContent)
	}
	if got := res.Header().Get("Access-Control-Allow-Origin"); got != "http://127.0.0.1:5174" {
		t.Fatalf("allow-origin = %q", got)
	}
}

func TestServeHTTPRejectsUntrustedOrigin(t *testing.T) {
	srv := &Server{mux: http.NewServeMux()}
	req := httptest.NewRequest(http.MethodPost, "/api/test", nil)
	req.Header.Set("Origin", "http://example.com")
	res := httptest.NewRecorder()
	srv.ServeHTTP(res, req)

	if res.Code != http.StatusForbidden {
		t.Fatalf("preflight status = %d, want %d", res.Code, http.StatusForbidden)
	}
}
