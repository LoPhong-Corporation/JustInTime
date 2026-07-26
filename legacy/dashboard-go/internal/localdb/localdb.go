// Package localdb reads the same SQLite database the C agent writes
// (activity_logs table, see database.c), read-only. This is the new
// piece the previous Python dashboard didn't have at all: a data source
// that works with zero network and zero login, since the agent already
// stores every activity record locally before ever trying to sync.
package localdb

import (
	"database/sql"
	"fmt"
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
