// Package aiinsights is a small, on-demand feature: it sends app-usage
// totals (from local SQLite or from Supabase, whichever the user picks)
// to the Google Gemini API and asks for a short summary, a category
// per app, and a few supportive recommendations. It only runs when the
// user clicks the button — never automatically, and never as a
// background job.
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
)

// Model is deliberately a small/fast/cheap model — this is a light
// summarization task, not something that needs a large model. Gemini
// renumbers its lineup and retires old models periodically (2.5 Flash
// was cut off for new API keys and is being shut down entirely on
// Oct 16, 2026) — if this ever 404s again, check
// https://ai.google.dev/gemini-api/docs/models for the current
// Flash/Flash-Lite model ID and update it here.
const Model = "gemini-3.5-flash-lite"

// AppUsage is a source-agnostic input row: either from
// internal/localdb (this device only) or aggregated from Supabase
// (internal/cloud, potentially spanning every device on the account).
type AppUsage struct {
	ProcessName  string
	TotalSeconds int64
}

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

const systemPrompt = `You are analyzing a person's own computer app-usage data (process names and total minutes used over roughly the last 7 days) to help THEM understand their own habits. This is self-reflection, not surveillance or a report to anyone else.

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

// Generate calls the Gemini API with the given usage totals and
// returns structured insights. apiKey is the caller's own Gemini API
// key (see Settings > AI Insights) — this package never embeds or
// shares a key of its own. sourceLabel is a short human-readable
// description of where the data came from (e.g. "this device (local)"
// or "all your devices (cloud)"), folded into the prompt so the
// summary can refer to it naturally.
func Generate(ctx context.Context, apiKey string, usage []AppUsage, sourceLabel string) (*Insights, error) {
	if apiKey == "" {
		return nil, fmt.Errorf("no Gemini API key configured — add one in Settings > AI Insights")
	}
	if len(usage) == 0 {
		return nil, fmt.Errorf("not enough activity data yet")
	}

	var sb strings.Builder
	fmt.Fprintf(&sb, "Data source: %s\n\n", sourceLabel)
	fmt.Fprintf(&sb, "App usage (process name: total minutes):\n\n")
	for _, u := range usage {
		fmt.Fprintf(&sb, "- %s: %d minutes\n", u.ProcessName, u.TotalSeconds/60)
	}

	reqBody, err := json.Marshal(map[string]any{
		"system_instruction": map[string]any{
			"parts": []map[string]string{{"text": systemPrompt}},
		},
		"contents": []map[string]any{
			{
				"role":  "user",
				"parts": []map[string]string{{"text": sb.String()}},
			},
		},
		"generationConfig": map[string]any{
			"response_mime_type": "application/json",
		},
	})
	if err != nil {
		return nil, err
	}

	url := fmt.Sprintf(
		"https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
		Model, apiKey,
	)

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, url, bytes.NewReader(reqBody))
	if err != nil {
		return nil, err
	}
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
		return nil, fmt.Errorf("Gemini API returned HTTP %d: %s", resp.StatusCode, string(respBytes))
	}

	var apiResp struct {
		Candidates []struct {
			Content struct {
				Parts []struct {
					Text string `json:"text"`
				} `json:"parts"`
			} `json:"content"`
		} `json:"candidates"`
	}
	if err := json.Unmarshal(respBytes, &apiResp); err != nil {
		return nil, fmt.Errorf("could not parse Gemini API response: %w", err)
	}
	if len(apiResp.Candidates) == 0 || len(apiResp.Candidates[0].Content.Parts) == 0 {
		return nil, fmt.Errorf("Gemini API returned no content (possibly blocked by safety filters)")
	}

	var rawText strings.Builder
	for _, part := range apiResp.Candidates[0].Content.Parts {
		rawText.WriteString(part.Text)
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

// ---------------------------------------------------------------------
// Chat: a small Gemini-powered assistant that answers free-form
// questions about the person's OWN usage data ("how much Discord did
// I use this week?"). Same model/key/never-automatic principles as
// Generate() above - this only runs when the person sends a message.
// ---------------------------------------------------------------------

type ChatMessage struct {
	// Role is "user" or "model" (Gemini's naming for the assistant
	// turn) - the chat history as the person has seen it so far.
	Role string `json:"role"`
	Text string `json:"text"`
}

const chatSystemPrompt = `You are a small assistant embedded in JustInTime, a personal computer-activity tracker. You answer the person's questions about THEIR OWN usage data - which apps they used, for how long, and roughly when - using ONLY the data block provided below. This is self-reflection for the person themselves, not a report to anyone else.

Rules:
- Only use the data given to you below. Never invent numbers, app names, or dates that aren't in it.
- If the data doesn't cover what's being asked (e.g. they ask about a date range you weren't given), say so plainly instead of guessing.
- Keep answers short and conversational - a sentence or two for simple questions, a short paragraph at most for anything more involved. This is a chat, not a report.
- Be warm and non-judgmental about how they spend their time - you're here to inform, not to lecture.
- If asked something totally unrelated to their usage data (general knowledge, coding help, etc.), gently redirect: you're only set up to talk about their JustInTime activity data.

Usage data (process name: total minutes, most recent ~14 days, local device time):`

// Chat sends the conversation so far plus a fresh usage-data snapshot
// to Gemini and returns the assistant's next reply as plain text (no
// JSON schema this time - it's meant to be read directly).
func Chat(ctx context.Context, apiKey string, history []ChatMessage, usage []AppUsage) (string, error) {
	if apiKey == "" {
		return "", fmt.Errorf("no Gemini API key configured — add one in Settings > AI Insights")
	}
	if len(history) == 0 {
		return "", fmt.Errorf("empty message")
	}

	var dataBlock strings.Builder
	if len(usage) == 0 {
		dataBlock.WriteString("(no activity recorded yet)")
	} else {
		for _, u := range usage {
			fmt.Fprintf(&dataBlock, "- %s: %d minutes\n", u.ProcessName, u.TotalSeconds/60)
		}
	}

	contents := make([]map[string]any, 0, len(history))
	for _, m := range history {
		role := m.Role
		if role != "user" && role != "model" {
			role = "user"
		}
		contents = append(contents, map[string]any{
			"role":  role,
			"parts": []map[string]string{{"text": m.Text}},
		})
	}

	reqBody, err := json.Marshal(map[string]any{
		"system_instruction": map[string]any{
			"parts": []map[string]string{{"text": chatSystemPrompt + "\n\n" + dataBlock.String()}},
		},
		"contents": contents,
	})
	if err != nil {
		return "", err
	}

	url := fmt.Sprintf(
		"https://generativelanguage.googleapis.com/v1beta/models/%s:generateContent?key=%s",
		Model, apiKey,
	)

	req, err := http.NewRequestWithContext(ctx, http.MethodPost, url, bytes.NewReader(reqBody))
	if err != nil {
		return "", err
	}
	req.Header.Set("Content-Type", "application/json")

	client := &http.Client{Timeout: 30 * time.Second}

	resp, err := client.Do(req)
	if err != nil {
		return "", err
	}
	defer resp.Body.Close()

	respBytes, err := io.ReadAll(resp.Body)
	if err != nil {
		return "", err
	}

	if resp.StatusCode != http.StatusOK {
		return "", fmt.Errorf("Gemini API returned HTTP %d: %s", resp.StatusCode, string(respBytes))
	}

	var apiResp struct {
		Candidates []struct {
			Content struct {
				Parts []struct {
					Text string `json:"text"`
				} `json:"parts"`
			} `json:"content"`
		} `json:"candidates"`
	}
	if err := json.Unmarshal(respBytes, &apiResp); err != nil {
		return "", fmt.Errorf("could not parse Gemini API response: %w", err)
	}
	if len(apiResp.Candidates) == 0 || len(apiResp.Candidates[0].Content.Parts) == 0 {
		return "", fmt.Errorf("Gemini API returned no content (possibly blocked by safety filters)")
	}

	var out strings.Builder
	for _, part := range apiResp.Candidates[0].Content.Parts {
		out.WriteString(part.Text)
	}

	return strings.TrimSpace(out.String()), nil
}
