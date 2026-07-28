// Package aiinsights is a small, on-demand feature: it sends the
// user's own local app-usage totals (never sent anywhere else) to the
// Anthropic API and asks for a short summary, a category per app, and
// a few supportive recommendations. It only runs when the user clicks
// the button — never automatically, and never as a background job —
// since it costs an API call and leaves the machine (unlike everything
// else in the Local Activity tab, which stays fully offline).
package aiinsights

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"strings"
	"time"

	"justintime-dashboard/internal/localdb"
)

// Model is deliberately a small/cheap/fast model — this is a light
// summarization task, not something that needs a large model.
const Model = "claude-haiku-4-5-20251001"

type AppInsight struct {
	ProcessName string `json:"process_name"`
	Category    string `json:"category"`
	Note        string `json:"note"`
}

type Insights struct {
	Summary         string       `json:"summary"`
	Apps            []AppInsight `json:"apps"`
	Recommendations []string     `json:"recommendations"`
}

const systemPrompt = `You are analyzing a person's own computer app-usage data (process names and total minutes used over the last 7 days) to help THEM understand their own habits. This is self-reflection, not surveillance or a report to anyone else.

Respond with ONLY a JSON object matching this exact schema — no markdown code fences, no extra commentary before or after:
{
  "summary": "2-3 sentence plain-language summary of their usage pattern, written directly to the person (use \"you\")",
  "apps": [{"process_name": "...", "category": "one of: Productivity, Development, Communication, Entertainment, Gaming, Browsing, System, Other", "note": "a short 5-10 word note about this app"}],
  "recommendations": ["short, kind, actionable suggestion", "..."]
}

Guidelines:
- Categorize every app given, even if the name is unfamiliar — make a reasonable guess from the process name.
- Keep recommendations supportive and non-judgmental. Never assume something is "bad" just because it's entertainment or gaming — balance matters more than any single category.
- If the usage pattern already looks healthy and balanced, say so instead of inventing a problem.
- 2-4 recommendations is plenty; don't pad the list.`

// Generate calls the Anthropic API with the given usage totals and
// returns structured insights. apiKey is the caller's own Anthropic
// API key (see Settings > AI Insights) — this package never embeds or
// shares a key of its own.
func Generate(ctx context.Context, apiKey string, usage []localdb.AppUsage) (*Insights, error) {
	if apiKey == "" {
		return nil, fmt.Errorf("no Anthropic API key configured — add one in Settings > AI Insights")
	}
	if len(usage) == 0 {
		return nil, fmt.Errorf("not enough local activity data yet")
	}

	var sb strings.Builder
	for _, u := range usage {
		fmt.Fprintf(&sb, "- %s: %d minutes\n", u.ProcessName, u.TotalSeconds/60)
	}

	userPrompt := "Here is the app usage data (process name: total minutes over the last 7 days):\n\n" + sb.String()

	reqBody, err := json.Marshal(map[string]any{
		"model":      Model,
		"max_tokens": 1024,
		"system":     systemPrompt,
		"messages": []map[string]string{
			{"role": "user", "content": userPrompt},
		},
	})
	if err != nil {
		return nil, err
	}

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, "https://api.anthropic.com/v1/messages", bytes.NewReader(reqBody))
	if err != nil {
		return nil, err
	}
	req.Header.Set("x-api-key", apiKey)
	req.Header.Set("anthropic-version", "2023-06-01")
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{Timeout: 30 * time.Second}

	resp, err := client.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()

	respBytes, err := io.ReadAll(resp.Body)
	if err != nil {
		return nil, err
	}

	if resp.StatusCode != http.StatusOK {
		return nil, fmt.Errorf("Anthropic API returned HTTP %d: %s", resp.StatusCode, string(respBytes))
	}

	var apiResp struct {
		Content []struct {
			Type string `json:"type"`
			Text string `json:"text"`
		} `json:"content"`
	}
	if err := json.Unmarshal(respBytes, &apiResp); err != nil {
		return nil, fmt.Errorf("could not parse Anthropic API response: %w", err)
	}

	var rawText strings.Builder
	for _, c := range apiResp.Content {
		if c.Type == "text" {
			rawText.WriteString(c.Text)
		}
	}

	text := strings.TrimSpace(rawText.String())
	text = strings.TrimPrefix(text, "```json")
	text = strings.TrimPrefix(text, "```")
	text = strings.TrimSuffix(text, "```")
	text = strings.TrimSpace(text)

	var insights Insights
	if err := json.Unmarshal([]byte(text), &insights); err != nil {
		return nil, fmt.Errorf("could not parse the model's JSON response: %w", err)
	}

	return &insights, nil
}
