use chrono::{Duration as ChronoDuration, Local, NaiveDate, TimeZone};
use rusqlite::{Connection, OpenFlags, params};
use serde::Serialize;
use std::{
    env,
    path::{Path, PathBuf},
    time::Duration,
};

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct AppUsage {
    process_name: String,
    total_seconds: i64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct DailyTotal {
    date: String,
    total_seconds: i64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Activity {
    process_name: String,
    window_title: String,
    duration_seconds: i64,
    start_time: i64,
    end_time: i64,
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct LocalDashboardData {
    available: bool,
    today_seconds: i64,
    yesterday_seconds: i64,
    today_activity_count: i64,
    application_count: i64,
    hourly_totals: Vec<i64>,
    daily_totals: Vec<DailyTotal>,
    app_usage: Vec<AppUsage>,
    recent_activities: Vec<Activity>,
}

impl LocalDashboardData {
    fn unavailable() -> Self {
        Self {
            available: false,
            today_seconds: 0,
            yesterday_seconds: 0,
            today_activity_count: 0,
            application_count: 0,
            hourly_totals: vec![0; 24],
            daily_totals: Vec::new(),
            app_usage: Vec::new(),
            recent_activities: Vec::new(),
        }
    }
}

fn local_day_start(date: NaiveDate) -> Result<i64, String> {
    for minute in 0..=180 {
        let local_time = date
            .and_hms_opt(0, 0, 0)
            .ok_or_else(|| "Không thể xác định đầu ngày địa phương.".to_string())?
            + ChronoDuration::minutes(minute);
        if let Some(time) = Local.from_local_datetime(&local_time).earliest() {
            return Ok(time.timestamp());
        }
    }
    Err("Không thể xác định múi giờ địa phương cho ngày này.".to_string())
}

fn read_local_dashboard_data() -> Result<LocalDashboardData, String> {
    let app_data = env::var_os("APPDATA")
        .ok_or_else(|| "Không tìm thấy thư mục APPDATA của Windows.".to_string())?;
    let database_path = PathBuf::from(app_data)
        .join("JustInTime")
        .join("justintime.db");
    read_local_dashboard_data_from_path(&database_path)
}

fn read_local_dashboard_data_from_path(database_path: &Path) -> Result<LocalDashboardData, String> {
    if !database_path.is_file() {
        return Ok(LocalDashboardData::unavailable());
    }

    let connection = Connection::open_with_flags(
        &database_path,
        OpenFlags::SQLITE_OPEN_READ_ONLY | OpenFlags::SQLITE_OPEN_NO_MUTEX,
    )
    .map_err(|error| format!("Không thể mở cơ sở dữ liệu hoạt động: {error}"))?;
    connection
        .busy_timeout(Duration::from_secs(3))
        .map_err(|error| format!("Không thể chờ cơ sở dữ liệu hoạt động: {error}"))?;

    let today = Local::now().date_naive();
    let today_start = local_day_start(today)?;
    let tomorrow_start = local_day_start(today + ChronoDuration::days(1))?;
    let yesterday_start = local_day_start(today - ChronoDuration::days(1))?;

    let total_for_period = |start: i64, end: i64| -> Result<i64, String> {
        connection
            .query_row(
                "SELECT COALESCE(SUM(duration_seconds), 0)
                 FROM activity_logs WHERE start_time >= ?1 AND start_time < ?2;",
                params![start, end],
                |row| row.get(0),
            )
            .map_err(|error| format!("Không thể đọc tổng thời gian sử dụng: {error}"))
    };

    let today_seconds = total_for_period(today_start, tomorrow_start)?;
    let yesterday_seconds = total_for_period(yesterday_start, today_start)?;
    let today_activity_count = connection
        .query_row(
            "SELECT COUNT(*) FROM activity_logs WHERE start_time >= ?1 AND start_time < ?2;",
            params![today_start, tomorrow_start],
            |row| row.get(0),
        )
        .map_err(|error| format!("Không thể đọc số hoạt động hôm nay: {error}"))?;
    let application_count = connection
        .query_row(
            "SELECT COUNT(DISTINCT process_name) FROM activity_logs
             WHERE start_time >= ?1 AND start_time < ?2;",
            params![today_start, tomorrow_start],
            |row| row.get(0),
        )
        .map_err(|error| format!("Không thể đọc số ứng dụng hôm nay: {error}"))?;

    let mut hourly_totals = vec![0_i64; 24];
    let mut hourly_query = connection
        .prepare(
            "SELECT CAST(strftime('%H', start_time, 'unixepoch', 'localtime') AS INTEGER),
                    SUM(duration_seconds)
             FROM activity_logs WHERE start_time >= ?1 AND start_time < ?2
             GROUP BY 1;",
        )
        .map_err(|error| format!("Không thể chuẩn bị dữ liệu theo giờ: {error}"))?;
    let hourly_rows = hourly_query
        .query_map(params![today_start, tomorrow_start], |row| {
            Ok((row.get::<_, i64>(0)?, row.get::<_, i64>(1)?))
        })
        .map_err(|error| format!("Không thể đọc dữ liệu theo giờ: {error}"))?;
    for row in hourly_rows {
        let (hour, seconds) =
            row.map_err(|error| format!("Không thể đọc dữ liệu theo giờ: {error}"))?;
        if let Ok(hour) = usize::try_from(hour) {
            if let Some(total) = hourly_totals.get_mut(hour) {
                *total = seconds;
            }
        }
    }

    let mut daily_totals = Vec::with_capacity(7);
    for days_ago in (0..7).rev() {
        let date = today - ChronoDuration::days(days_ago);
        let start = local_day_start(date)?;
        let end = local_day_start(date + ChronoDuration::days(1))?;
        daily_totals.push(DailyTotal {
            date: date.format("%Y-%m-%d").to_string(),
            total_seconds: total_for_period(start, end)?,
        });
    }

    let mut app_query = connection
        .prepare(
            "SELECT process_name, SUM(duration_seconds) AS total
             FROM activity_logs WHERE start_time >= ?1 AND start_time < ?2
             GROUP BY process_name ORDER BY total DESC LIMIT 50;",
        )
        .map_err(|error| format!("Không thể chuẩn bị danh sách ứng dụng: {error}"))?;
    let app_usage = app_query
        .query_map(params![today_start, tomorrow_start], |row| {
            Ok(AppUsage {
                process_name: row.get(0)?,
                total_seconds: row.get(1)?,
            })
        })
        .map_err(|error| format!("Không thể đọc danh sách ứng dụng: {error}"))?
        .collect::<Result<Vec<_>, _>>()
        .map_err(|error| format!("Không thể đọc danh sách ứng dụng: {error}"))?;

    let mut activity_query = connection
        .prepare(
            "SELECT process_name, window_title, duration_seconds, start_time, end_time
             FROM activity_logs ORDER BY start_time DESC LIMIT 100;",
        )
        .map_err(|error| format!("Không thể chuẩn bị lịch sử hoạt động: {error}"))?;
    let recent_activities = activity_query
        .query_map([], |row| {
            Ok(Activity {
                process_name: row.get(0)?,
                window_title: row.get(1)?,
                duration_seconds: row.get(2)?,
                start_time: row.get(3)?,
                end_time: row.get(4)?,
            })
        })
        .map_err(|error| format!("Không thể đọc lịch sử hoạt động: {error}"))?
        .collect::<Result<Vec<_>, _>>()
        .map_err(|error| format!("Không thể đọc lịch sử hoạt động: {error}"))?;

    Ok(LocalDashboardData {
        available: true,
        today_seconds,
        yesterday_seconds,
        today_activity_count,
        application_count,
        hourly_totals,
        daily_totals,
        app_usage,
        recent_activities,
    })
}

#[tauri::command]
fn get_local_dashboard_data() -> Result<LocalDashboardData, String> {
    read_local_dashboard_data()
}

#[cfg_attr(mobile, tauri::mobile_entry_point)]
pub fn run() {
    tauri::Builder::default()
        .plugin(tauri_plugin_opener::init())
        .invoke_handler(tauri::generate_handler![get_local_dashboard_data])
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn reads_local_usage_and_history_from_read_only_database() {
        let unique = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let path = env::temp_dir().join(format!("jitdashboard-{unique}.db"));
        let today_start = local_day_start(Local::now().date_naive()).unwrap();
        let yesterday_start =
            local_day_start(Local::now().date_naive() - ChronoDuration::days(1)).unwrap();

        let connection = Connection::open(&path).unwrap();
        connection
            .execute_batch(
                "CREATE TABLE activity_logs (
                    process_name TEXT NOT NULL,
                    window_title TEXT NOT NULL,
                    duration_seconds INTEGER NOT NULL,
                    start_time INTEGER NOT NULL,
                    end_time INTEGER NOT NULL,
                    synced INTEGER DEFAULT 0
                );",
            )
            .unwrap();
        connection
            .execute(
                "INSERT INTO activity_logs
                 (process_name, window_title, duration_seconds, start_time, end_time)
                 VALUES (?1, ?2, ?3, ?4, ?5);",
                params![
                    "test-app.exe",
                    "Test window",
                    1200,
                    today_start + 600,
                    today_start + 1800
                ],
            )
            .unwrap();
        connection
            .execute(
                "INSERT INTO activity_logs
                 (process_name, window_title, duration_seconds, start_time, end_time)
                 VALUES (?1, ?2, ?3, ?4, ?5);",
                params![
                    "yesterday-app.exe",
                    "Yesterday window",
                    600,
                    yesterday_start + 600,
                    yesterday_start + 1200
                ],
            )
            .unwrap();
        drop(connection);

        let result = read_local_dashboard_data_from_path(&path);
        let _ = std::fs::remove_file(&path);
        let data = result.unwrap();

        assert!(data.available);
        assert_eq!(data.today_seconds, 1200);
        assert_eq!(data.yesterday_seconds, 600);
        assert_eq!(data.today_activity_count, 1);
        assert_eq!(data.application_count, 1);
        assert_eq!(data.hourly_totals.iter().sum::<i64>(), 1200);
        assert_eq!(data.daily_totals.len(), 7);
        assert_eq!(data.daily_totals[6].total_seconds, 1200);
        assert_eq!(data.app_usage[0].process_name, "test-app.exe");
        assert_eq!(data.recent_activities.len(), 2);
    }
}
