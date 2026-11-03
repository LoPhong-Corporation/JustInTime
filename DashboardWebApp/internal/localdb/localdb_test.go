package localdb

import (
	"database/sql"
	"os"
	"testing"

	_ "modernc.org/sqlite"
)

func TestActivitiesInRange(t *testing.T) {
	file, err := os.CreateTemp(t.TempDir(), "activity-*.db")
	if err != nil {
		t.Fatal(err)
	}
	path := file.Name()
	if err := file.Close(); err != nil {
		t.Fatal(err)
	}

	writer, err := sql.Open("sqlite", path)
	if err != nil {
		t.Fatal(err)
	}
	_, err = writer.Exec(`CREATE TABLE activity_logs (
		process_name TEXT NOT NULL,
		window_title TEXT NOT NULL,
		duration_seconds INTEGER NOT NULL,
		start_time INTEGER NOT NULL,
		end_time INTEGER NOT NULL,
		synced INTEGER NOT NULL DEFAULT 0
	);`)
	if err != nil {
		_ = writer.Close()
		t.Fatal(err)
	}
	for _, row := range []struct {
		name string
		time int64
	}{
		{name: "before.exe", time: 99},
		{name: "first.exe", time: 100},
		{name: "last.exe", time: 199},
		{name: "after.exe", time: 200},
	} {
		if _, err := writer.Exec(
			`INSERT INTO activity_logs
			 (process_name, window_title, duration_seconds, start_time, end_time, synced)
			 VALUES (?, ?, ?, ?, ?, ?);`,
			row.name, "window", 60, row.time, row.time+60, 1,
		); err != nil {
			_ = writer.Close()
			t.Fatal(err)
		}
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}

	db, err := Open(path)
	if err != nil {
		t.Fatal(err)
	}
	defer db.Close()

	activities, err := db.ActivitiesInRange(100, 200, 1)
	if err != nil {
		t.Fatal(err)
	}
	if len(activities) != 1 || activities[0].ProcessName != "last.exe" || !activities[0].Synced {
		t.Fatalf("unexpected range result: %#v", activities)
	}
}
