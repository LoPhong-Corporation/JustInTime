// Package localdb reads the same SQLite database the C agent writes
// (activity_logs table, see database.c), read-only. This is the new
// piece the previous Python dashboard didn't have at all: a data source
// that works with zero network and zero login, since the agent already
// stores every activity record locally before ever trying to sync.
package localdb

import (
	"database/sql"
	"fmt"
	"sort"
	"time"

	_ "modernc.org/sqlite" // pure-Go driver, no cgo needed to cross-compile for Windows
)

type DB struct {
	sql *sql.DB
}

type AppUsage struct {
	ProcessName  string `json:"process_name"`
	TotalSeconds int64  `json:"total_seconds"`
}

type Activity struct {
	ProcessName string `json:"process_name"`
	WindowTitle string `json:"window_title"`
	Duration    int64  `json:"duration_seconds"`
	StartTime   int64  `json:"start_time"`
	EndTime     int64  `json:"end_time"`
	Synced      bool   `json:"synced"`
}

type WindowUsage struct {
	WindowTitle  string `json:"window_title"`
	TotalSeconds int64  `json:"total_seconds"`
}

type AppUsageDetails struct {
	ProcessName          string        `json:"process_name"`
	TodaySeconds         int64         `json:"today_seconds"`
	YesterdaySeconds     int64         `json:"yesterday_seconds"`
	WeekSeconds          int64         `json:"week_seconds"`
	SessionCount         int           `json:"session_count"`
	AverageSessionSecond int64         `json:"average_session_seconds"`
	LongestSessionSecond int64         `json:"longest_session_seconds"`
	HourlySeconds        []int64       `json:"hourly_seconds"`
	DailyTotals          []DayTotal    `json:"daily_totals"`
	Recent               []Activity    `json:"recent"`
	Windows              []WindowUsage `json:"windows"`
}

// Open opens the database read-only so the dashboard can never corrupt
// or lock the file the agent depends on.
func Open(path string) (*DB, error) {
	dsn := fmt.Sprintf("file:%s?mode=ro&_pragma=busy_timeout(3000)", path)

	sqlDB, err := sql.Open("sqlite", dsn)
	if err != nil {
		return nil, err
	}
	if err := sqlDB.Ping(); err != nil {
		sqlDB.Close()
		return nil, err
	}
	return &DB{sql: sqlDB}, nil
}

func (d *DB) Close() error {
	if d == nil || d.sql == nil {
		return nil
	}
	return d.sql.Close()
}

func dayRange(daysAgo int) (int64, int64) {
	now := time.Now()
	end := time.Date(now.Year(), now.Month(), now.Day(), 0, 0, 0, 0, now.Location()).Add(24 * time.Hour)
	start := end.AddDate(0, 0, -daysAgo-1)
	return start.Unix(), end.Unix()
}

// Usage returns total seconds spent per process within the last
// `days` days (days=0 means "today only"), most-used first.
func (d *DB) Usage(days int) ([]AppUsage, error) {
	if d == nil || d.sql == nil {
		return nil, fmt.Errorf("local database not available")
	}

	start, end := dayRange(days)

	rows, err := d.sql.Query(
		`SELECT process_name, SUM(duration_seconds) AS total
		 FROM activity_logs
		 WHERE start_time >= ? AND start_time < ?
		 GROUP BY process_name
		 ORDER BY total DESC
		 LIMIT 20`,
		start, end,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var out []AppUsage
	for rows.Next() {
		var u AppUsage
		if err := rows.Scan(&u.ProcessName, &u.TotalSeconds); err != nil {
			return nil, err
		}
		out = append(out, u)
	}
	return out, rows.Err()
}

// RecentActivities returns the most recent activity_logs rows, newest
// first — the local-mode equivalent of /api/cloud/recent.
func (d *DB) RecentActivities(limit int) ([]Activity, error) {
	if d == nil || d.sql == nil {
		return nil, fmt.Errorf("local database not available")
	}

	rows, err := d.sql.Query(
		`SELECT process_name, window_title, duration_seconds, start_time, end_time, synced
		 FROM activity_logs
		 ORDER BY start_time DESC
		 LIMIT ?`,
		limit,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var out []Activity
	for rows.Next() {
		var a Activity
		var synced int
		if err := rows.Scan(&a.ProcessName, &a.WindowTitle, &a.Duration, &a.StartTime, &a.EndTime, &synced); err != nil {
			return nil, err
		}
		a.Synced = synced != 0
		out = append(out, a)
	}
	return out, rows.Err()
}

// UsageInRange returns total seconds spent per process within
// [start, end) (unix seconds), most-used first — the general-purpose
// version of Usage(), used by the history period picker (today,
// yesterday, this week, last week, custom) so every period goes
// through one code path instead of Usage()'s fixed "last N days"
// window.
func (d *DB) UsageInRange(start, end int64) ([]AppUsage, error) {
	if d == nil || d.sql == nil {
		return nil, fmt.Errorf("local database not available")
	}

	rows, err := d.sql.Query(
		`SELECT process_name, SUM(duration_seconds) AS total
		 FROM activity_logs
		 WHERE start_time >= ? AND start_time < ?
		 GROUP BY process_name
		 ORDER BY total DESC
		 LIMIT 20`,
		start, end,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var out []AppUsage
	for rows.Next() {
		var u AppUsage
		if err := rows.Scan(&u.ProcessName, &u.TotalSeconds); err != nil {
			return nil, err
		}
		out = append(out, u)
	}
	return out, rows.Err()
}

// DayTotal is total tracked seconds for one calendar day - used for
// the "This week" / "Last week" bar-per-day view (a single 24h
// timeline strip doesn't make sense across multiple days).
type DayTotal struct {
	Date         string `json:"date"` // "2006-01-02", local time
	TotalSeconds int64  `json:"total_seconds"`
}

// DailyTotalsInRange returns one row per calendar day, given the
// unix-second boundaries of each day (dayBoundaries[i] to
// dayBoundaries[i+1]) - the caller (server.go) computes these using
// the server's local time zone, so this function stays a plain query
// with no date-math of its own.
func (d *DB) DailyTotalsInRange(dayBoundaries []int64) ([]DayTotal, error) {
	if d == nil || d.sql == nil {
		return nil, fmt.Errorf("local database not available")
	}
	if len(dayBoundaries) < 2 {
		return nil, nil
	}

	out := make([]DayTotal, 0, len(dayBoundaries)-1)

	for i := 0; i < len(dayBoundaries)-1; i++ {
		start := dayBoundaries[i]
		end := dayBoundaries[i+1]

		var total sql.NullInt64
		row := d.sql.QueryRow(
			`SELECT SUM(duration_seconds) FROM activity_logs WHERE start_time >= ? AND start_time < ?`,
			start, end,
		)
		if err := row.Scan(&total); err != nil {
			return nil, err
		}

		out = append(out, DayTotal{
			Date:         time.Unix(start, 0).Format("2006-01-02"),
			TotalSeconds: total.Int64,
		})
	}

	return out, nil
}

// ActivitiesForDay returns every activity_logs row whose start_time
// falls within [dayStart, dayEnd) (unix seconds, caller's local day
// boundaries), oldest first - built for the visual Timeline (see
// handleTimeline in server.go), which needs every segment across the
// day in order, not just the most recent N like RecentActivities.
func (d *DB) ActivitiesForDay(dayStart, dayEnd int64) ([]Activity, error) {
	if d == nil || d.sql == nil {
		return nil, fmt.Errorf("local database not available")
	}

	rows, err := d.sql.Query(
		`SELECT process_name, window_title, duration_seconds, start_time, end_time, synced
		 FROM activity_logs
		 WHERE start_time >= ? AND start_time < ?
		 ORDER BY start_time ASC`,
		dayStart, dayEnd,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var out []Activity
	for rows.Next() {
		var a Activity
		var synced int
		if err := rows.Scan(&a.ProcessName, &a.WindowTitle, &a.Duration, &a.StartTime, &a.EndTime, &synced); err != nil {
			return nil, err
		}
		a.Synced = synced != 0
		out = append(out, a)
	}
	return out, rows.Err()
}

// ActivitiesInRange returns activity rows whose start_time falls within
// [start, end), newest first, capped to keep exports bounded.
func (d *DB) ActivitiesInRange(start, end int64, limit int) ([]Activity, error) {
	if d == nil || d.sql == nil {
		return nil, fmt.Errorf("local database not available")
	}
	if limit <= 0 || limit > 10000 {
		limit = 10000
	}

	rows, err := d.sql.Query(
		`SELECT process_name, window_title, duration_seconds, start_time, end_time, synced
		 FROM activity_logs
		 WHERE start_time >= ? AND start_time < ?
		 ORDER BY start_time DESC
		 LIMIT ?`,
		start, end, limit,
	)
	if err != nil {
		return nil, err
	}
	defer rows.Close()

	var out []Activity
	for rows.Next() {
		var activity Activity
		var synced int
		if err := rows.Scan(
			&activity.ProcessName,
			&activity.WindowTitle,
			&activity.Duration,
			&activity.StartTime,
			&activity.EndTime,
			&synced,
		); err != nil {
			return nil, err
		}
		activity.Synced = synced != 0
		out = append(out, activity)
	}
	return out, rows.Err()
}

// AppDetails returns real per-application session, window-title, and
// time-of-day totals for the last seven calendar days.
func (d *DB) AppDetails(processName string, dayStart, dayEnd int64) (AppUsageDetails, error) {
	if d == nil || d.sql == nil {
		return AppUsageDetails{}, fmt.Errorf("local database not available")
	}

	loc := time.Unix(dayStart, 0).Location()
	weekStart := time.Unix(dayStart, 0).AddDate(0, 0, -6).Unix()
	yesterdayStart := time.Unix(dayStart, 0).AddDate(0, 0, -1).Unix()
	rows, err := d.sql.Query(
		`SELECT process_name, window_title, duration_seconds, start_time, end_time, synced
		 FROM activity_logs
		 WHERE process_name = ? AND start_time >= ? AND start_time < ?
		 ORDER BY start_time DESC`,
		processName, weekStart, dayEnd,
	)
	if err != nil {
		return AppUsageDetails{}, err
	}
	defer rows.Close()

	daily := make([]DayTotal, 7)
	for i := range daily {
		date := time.Unix(weekStart, 0).In(loc).AddDate(0, 0, i)
		daily[i].Date = date.Format("2006-01-02")
	}
	dailyIndex := make(map[string]int, len(daily))
	for i, day := range daily {
		dailyIndex[day.Date] = i
	}

	detail := AppUsageDetails{
		ProcessName:   processName,
		HourlySeconds: make([]int64, 24),
		DailyTotals:   daily,
		Recent:        make([]Activity, 0, 20),
	}
	windowTotals := make(map[string]int64)
	var sessionTotal int64

	for rows.Next() {
		var activity Activity
		var synced int
		if err := rows.Scan(
			&activity.ProcessName,
			&activity.WindowTitle,
			&activity.Duration,
			&activity.StartTime,
			&activity.EndTime,
			&synced,
		); err != nil {
			return AppUsageDetails{}, err
		}
		activity.Synced = synced != 0
		if activity.Duration <= 0 {
			continue
		}

		start := time.Unix(activity.StartTime, 0).In(loc)
		dateKey := start.Format("2006-01-02")
		if index, ok := dailyIndex[dateKey]; ok {
			detail.DailyTotals[index].TotalSeconds += activity.Duration
			detail.WeekSeconds += activity.Duration
		}
		if activity.StartTime >= dayStart {
			detail.TodaySeconds += activity.Duration
			detail.SessionCount++
			sessionTotal += activity.Duration
			if activity.Duration > detail.LongestSessionSecond {
				detail.LongestSessionSecond = activity.Duration
			}
			if len(detail.Recent) < 20 {
				detail.Recent = append(detail.Recent, activity)
			}
			if activity.WindowTitle != "" {
				windowTotals[activity.WindowTitle] += activity.Duration
			}
		} else if activity.StartTime >= yesterdayStart {
			detail.YesterdaySeconds += activity.Duration
		}

		intervalStart := activity.StartTime
		if intervalStart < dayStart {
			intervalStart = dayStart
		}
		intervalEnd := activity.StartTime + activity.Duration
		if intervalEnd > dayEnd {
			intervalEnd = dayEnd
		}
		for cursor := intervalStart; cursor < intervalEnd; {
			current := time.Unix(cursor, 0).In(loc)
			nextHour := time.Date(current.Year(), current.Month(), current.Day(), current.Hour(), 0, 0, 0, loc).Add(time.Hour)
			boundary := nextHour.Unix()
			if boundary <= cursor {
				break
			}
			if boundary > intervalEnd {
				boundary = intervalEnd
			}
			detail.HourlySeconds[current.Hour()] += boundary - cursor
			cursor = boundary
		}
	}
	if err := rows.Err(); err != nil {
		return AppUsageDetails{}, err
	}
	if detail.SessionCount > 0 {
		detail.AverageSessionSecond = sessionTotal / int64(detail.SessionCount)
	}

	for title, seconds := range windowTotals {
		detail.Windows = append(detail.Windows, WindowUsage{WindowTitle: title, TotalSeconds: seconds})
	}
	sort.Slice(detail.Windows, func(i, j int) bool {
		return detail.Windows[i].TotalSeconds > detail.Windows[j].TotalSeconds
	})
	if len(detail.Windows) > 8 {
		detail.Windows = detail.Windows[:8]
	}
	return detail, nil
}
