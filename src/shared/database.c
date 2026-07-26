//
// Created by LoPhongCorporation on 6/24/2026.
//
#include "database.h"
#include "device.h"
#include "jsonutil.h"
#include "settings.h"
#include "error_codes.h"

#include "sqlite/sqlite3.h"

#include <windows.h>

#include <stdio.h>
#include <time.h>

static sqlite3* g_db = NULL;

/*
 * UTF16 -> UTF8
 */
static void wide_to_utf8(
    const wchar_t* wide,
    char* utf8,
    int utf8_size)
{
    WideCharToMultiByte(
        CP_UTF8,
        0,
        wide,
        -1,
        utf8,
        utf8_size,
        NULL,
        NULL
    );
}

static void utf8_to_wide(
    const char* utf8,
    wchar_t* wide,
    int wide_size)
{
    if (!utf8)
    {
        wide[0] = L'\0';
        return;
    }

    MultiByteToWideChar(
        CP_UTF8,
        0,
        utf8,
        -1,
        wide,
        wide_size
    );
}

/*
 * Khởi tạo database
 *
 * QUAN TRỌNG: dùng đường dẫn TUYỆT ĐỐI tại
 * %APPDATA%\JustInTime\justintime.db thay vì đường dẫn
 * tương đối "justintime.db" — vì khi app tự khởi động
 * cùng Windows (autostart), thư mục làm việc hiện tại
 * (CWD) có thể khác với thư mục chứa file .exe, khiến
 * app vô tình tạo 1 database RỖNG mới ở nơi khác, mất
 * hết dữ liệu cũ.
 */
int db_init(void)
{
    char dir[MAX_PATH];
    char db_path[MAX_PATH];

    if (settings_get_config_dir(dir, sizeof(dir)))
    {
        snprintf(
            db_path,
            sizeof(db_path),
            "%s\\justintime.db",
            dir
        );
    }
    else
    {
        /* Fallback hiếm khi xảy ra: %APPDATA% không đọc được */
        snprintf(db_path, sizeof(db_path), "justintime.db");
    }

    int rc = sqlite3_open(
        db_path,
        &g_db
    );

    if (rc != SQLITE_OK)
    {
        wprintf(
            L"[%hs] Failed to open database\n",
            ERR_DB_OPEN_FAIL
        );

        return 0;
    }

    const char* sql =
        "CREATE TABLE IF NOT EXISTS activity_logs ("
        "id INTEGER PRIMARY KEY AUTOINCREMENT,"

        "device_id TEXT NOT NULL,"

        "process_name TEXT NOT NULL,"
        "window_title TEXT NOT NULL,"

        "duration_seconds INTEGER NOT NULL,"

        "start_time INTEGER NOT NULL,"
        "end_time INTEGER NOT NULL,"


        "synced INTEGER DEFAULT 0,"

        "created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    char* error = NULL;

    rc = sqlite3_exec(
        g_db,
        sql,
        NULL,
        NULL,
        &error
    );

    if (rc != SQLITE_OK)
    {
        wprintf(
            L"[%hs] Create table failed: %hs\n",
            ERR_DB_CREATE_TABLE,
            error
        );

        sqlite3_free(error);

        return 0;
    }

    wprintf(
        L"Database initialized\n"
    );

    /*
     * Migration: thêm cột cho Retry Queue nếu chưa có
     * (ALTER TABLE ADD COLUMN lỗi vô hại nếu cột đã tồn tại,
     * nên cố tình bỏ qua kết quả trả về).
     */
    sqlite3_exec(
        g_db,
        "ALTER TABLE activity_logs ADD COLUMN retry_count INTEGER DEFAULT 0;",
        NULL, NULL, NULL
    );

    sqlite3_exec(
        g_db,
        "ALTER TABLE activity_logs ADD COLUMN next_retry_at INTEGER DEFAULT 0;",
        NULL, NULL, NULL
    );

    return 1;
}

/*
 * Lưu activity
 */
int db_insert_activity(
    const ActivityRecord* record)
{
    if (!record)
        return 0;

    char device_id[128] = {0};

    get_device_id(
        device_id,
        sizeof(device_id)
    );

    char process_utf8[512] = {0};
    char title_utf8[2048] = {0};

    wide_to_utf8(
        record->process_name,
        process_utf8,
        sizeof(process_utf8)
    );

    wide_to_utf8(
        record->window_title,
        title_utf8,
        sizeof(title_utf8)
    );

    const char* sql =
        "INSERT INTO activity_logs ("
        "device_id,"
        "process_name,"
        "window_title,"
        "duration_seconds,"
        "start_time,"
        "end_time,"
        "synced"
        ") "
        "VALUES (?, ?, ?, ?, ?, ?, ?);";

    sqlite3_stmt* stmt = NULL;

    int rc = sqlite3_prepare_v2(
        g_db,
        sql,
        -1,
        &stmt,
        NULL
    );

    if (rc != SQLITE_OK)
    {
        wprintf(
            L"[%hs] Prepare failed: %hs\n",
            ERR_DB_INSERT_FAIL,
            sqlite3_errmsg(g_db)
        );

        return 0;
    }

    sqlite3_bind_text(
        stmt,
        1,
        device_id,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        stmt,
        2,
        process_utf8,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_text(
        stmt,
        3,
        title_utf8,
        -1,
        SQLITE_TRANSIENT
    );

    sqlite3_bind_int64(
        stmt,
        4,
        record->duration_seconds
    );

    sqlite3_bind_int64(
        stmt,
        5,
        (sqlite3_int64)record->start_time
    );

    sqlite3_bind_int64(
        stmt,
        6,
        (sqlite3_int64)record->end_time
    );

    sqlite3_bind_int(
        stmt,
        7,
        record->synced
    );

    rc = sqlite3_step(
        stmt
    );

    sqlite3_finalize(
        stmt
    );

    if (rc != SQLITE_DONE)
    {
        wprintf(
            L"[%hs] Insert failed: %hs\n",
            ERR_DB_INSERT_FAIL,
            sqlite3_errmsg(g_db)
        );

        return 0;
    }

    wprintf(
        L"Activity saved\n"
    );

    return 1;
}

/*
 * Build noi dung bao cao tong thoi gian su dung moi app
 * hom nay vao buffer (dung chung cho console lan tray
 * MessageBox). Tra ve 1 neu co du lieu, 0 neu chua co.
 */
int db_build_daily_summary_text(
    wchar_t* out,
    int out_size)
{
    out[0] = L'\0';

    if (!g_db)
        return 0;

    time_t now = time(NULL);
    struct tm today;

    localtime_s(&today, &now);

    today.tm_hour = 0;
    today.tm_min  = 0;
    today.tm_sec  = 0;

    time_t day_start = mktime(&today);

    const char* sql =
        "SELECT process_name, SUM(duration_seconds) as total "
        "FROM activity_logs "
        "WHERE start_time >= ? "
        "GROUP BY process_name "
        "ORDER BY total DESC "
        "LIMIT 15;";

    sqlite3_stmt* stmt = NULL;

    if (
        sqlite3_prepare_v2(
            g_db,
            sql,
            -1,
            &stmt,
            NULL
        ) != SQLITE_OK
    )
        return 0;

    sqlite3_bind_int64(
        stmt,
        1,
        (sqlite3_int64)day_start
    );

    int has_row = 0;
    int pos = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        has_row = 1;

        const char* process_utf8 =
            (const char*)sqlite3_column_text(stmt, 0);

        long long total_seconds =
            sqlite3_column_int64(stmt, 1);

        wchar_t process_wide[512] = {0};

        utf8_to_wide(
            process_utf8,
            process_wide,
            512
        );

        long hours   = (long)(total_seconds / 3600);
        long minutes = (long)((total_seconds % 3600) / 60);
        long seconds = (long)(total_seconds % 60);

        int written = swprintf(
            out + pos,
            out_size - pos,
            L"%-28ls %02ld:%02ld:%02ld\n",
            process_wide,
            hours,
            minutes,
            seconds
        );

        if (written < 0)
            break;

        pos += written;
    }

    sqlite3_finalize(stmt);

    if (!has_row)
    {
        swprintf(
            out,
            out_size,
            L"(No data yet today)"
        );
    }

    return has_row;
}

long db_get_today_seconds(
    const wchar_t* process_name)
{
    if (!g_db || !process_name)
        return 0;

    time_t now = time(NULL);
    struct tm today;

    localtime_s(&today, &now);

    today.tm_hour = 0;
    today.tm_min  = 0;
    today.tm_sec  = 0;

    time_t day_start = mktime(&today);

    const char* sql =
        "SELECT COALESCE(SUM(duration_seconds), 0) "
        "FROM activity_logs "
        "WHERE process_name = ? AND start_time >= ?;";

    sqlite3_stmt* stmt = NULL;

    if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return 0;

    char process_utf8[512] = {0};
    wide_to_utf8(process_name, process_utf8, sizeof(process_utf8));

    sqlite3_bind_text(stmt, 1, process_utf8, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, (sqlite3_int64)day_start);

    long total = 0;

    if (sqlite3_step(stmt) == SQLITE_ROW)
        total = (long)sqlite3_column_int64(stmt, 0);

    sqlite3_finalize(stmt);

    return total;
}

/*
 * In bao cao tong thoi gian su dung moi app trong ngay
 * hom nay ra console (gop tat ca record du bi chia nho theo
 * tung lan doi window title). Khong thay doi cach luu chi
 * tiet tung record, chi tong hop luc hien thi.
 */
void db_print_daily_summary(void)
{
    wchar_t buffer[4096] = {0};

    db_build_daily_summary_text(
        buffer,
        4096
    );

    wprintf(
        L"\n"
        L"========== TONG KET HOM NAY (theo app) ==========\n"
        L"%ls"
        L"==================================================\n",
        buffer
    );
}
/*
 * Đóng database
 */
void db_close(void)
{
    if (g_db)
    {
        sqlite3_close(
            g_db
        );

        g_db = NULL;
    }
}

/*
 * Database print unsynced
 */
void db_print_unsynced(void)
{
    if (!g_db)
        return;

    const char* sql =
        "SELECT id, process_name, duration_seconds "
        "FROM activity_logs "
        "WHERE synced = 0;";

    sqlite3_stmt* stmt = NULL;

    if (sqlite3_prepare_v2(
            g_db,
            sql,
            -1,
            &stmt,
            NULL
        ) != SQLITE_OK)
    {
        return;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int id =
            sqlite3_column_int(stmt, 0);

        const char* process_utf8 =
            (const char*)sqlite3_column_text(stmt, 1);

        wchar_t process[512];

        utf8_to_wide(
            process_utf8,
            process,
            512
        );

        int duration =
            sqlite3_column_int(stmt, 2);

        wprintf(
            L"[UNSYNCED] id=%d process=%ls duration=%d\n",
            id,
            process,
            duration
        );
    }

    sqlite3_finalize(stmt);
}
int db_get_unsynced_records(
    SyncRecord* records,
    int max_records)
{
    if (!g_db)
        return 0;

    const char* sql =
        "SELECT "
        "id,"
        "device_id,"
        "process_name,"
        "window_title,"
        "duration_seconds,"
        "start_time,"
        "end_time "
        "FROM activity_logs "
        "WHERE synced = 0 "
        "AND next_retry_at <= CAST(strftime('%s','now') AS INTEGER) "
        "ORDER BY next_retry_at ASC "
        "LIMIT ?;";

    sqlite3_stmt* stmt = NULL;

    if (sqlite3_prepare_v2(
            g_db,
            sql,
            -1,
            &stmt,
            NULL
        ) != SQLITE_OK)
    {
        return 0;
    }

    sqlite3_bind_int(
        stmt,
        1,
        max_records
    );

    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW &&
           count < max_records)
    {
        SyncRecord* rec =
            &records[count];

        rec->id =
            sqlite3_column_int(stmt, 0);

        snprintf(
            rec->device_id,
            sizeof(rec->device_id),
            "%s",
            sqlite3_column_text(stmt, 1)
        );

        utf8_to_wide(
            (const char*)sqlite3_column_text(stmt, 2),
            rec->process_name,
            512
        );

        utf8_to_wide(
            (const char*)sqlite3_column_text(stmt, 3),
            rec->window_title,
            2048
        );

        rec->duration_seconds =
            sqlite3_column_int64(stmt, 4);

        rec->start_time =
            sqlite3_column_int64(stmt, 5);

        rec->end_time =
            sqlite3_column_int64(stmt, 6);

        count++;
    }

    sqlite3_finalize(stmt);

    return count;
}

int db_mark_synced(int id)
{
    const char* sql =
        "UPDATE activity_logs "
        "SET synced = 1 "
        "WHERE id = ?;";

    sqlite3_stmt* stmt = NULL;

    if (
        sqlite3_prepare_v2(
            g_db,
            sql,
            -1,
            &stmt,
            NULL
        ) != SQLITE_OK
    )
    {
        return 0;
    }

    sqlite3_bind_int(
        stmt,
        1,
        id
    );

    int rc =
        sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

/*
 * Đánh dấu 1 record vừa gửi lên cloud thất bại: tăng
 * retry_count và tính lại next_retry_at theo kiểu
 * exponential backoff (2^retry_count * base, giới hạn
 * bởi max), để không spam server liên tục khi mất mạng
 * dài ngày, đồng thời tự thử lại nhanh hơn khi vừa lỗi.
 */
int db_mark_sync_failed(int id)
{
    if (!g_db)
        return 0;

    AppSettings s;
    settings_get(&s);

    /* Lấy retry_count hiện tại */
    const char* select_sql =
        "SELECT retry_count FROM activity_logs WHERE id = ?;";

    sqlite3_stmt* select_stmt = NULL;
    int retry_count = 0;

    if (
        sqlite3_prepare_v2(g_db, select_sql, -1, &select_stmt, NULL)
        == SQLITE_OK
    )
    {
        sqlite3_bind_int(select_stmt, 1, id);

        if (sqlite3_step(select_stmt) == SQLITE_ROW)
            retry_count = sqlite3_column_int(select_stmt, 0);

        sqlite3_finalize(select_stmt);
    }

    retry_count++;

    /*
     * backoff = base * 2^retry_count, giới hạn bởi max.
     * Giới hạn retry_count dùng để tính lũy thừa ở 20 để
     * tránh tràn số nếu retry_count quá lớn theo thời gian.
     */
    int exp_cap = retry_count > 20 ? 20 : retry_count;
    long long backoff = (long long)s.retry_backoff_base_sec << exp_cap;

    if (backoff > s.retry_backoff_max_sec || backoff <= 0)
        backoff = s.retry_backoff_max_sec;

    const char* update_sql =
        "UPDATE activity_logs "
        "SET retry_count = ?, "
        "next_retry_at = CAST(strftime('%s','now') AS INTEGER) + ? "
        "WHERE id = ?;";

    sqlite3_stmt* update_stmt = NULL;

    if (
        sqlite3_prepare_v2(g_db, update_sql, -1, &update_stmt, NULL)
        != SQLITE_OK
    )
        return 0;

    sqlite3_bind_int(update_stmt, 1, retry_count);
    sqlite3_bind_int64(update_stmt, 2, backoff);
    sqlite3_bind_int(update_stmt, 3, id);

    int rc = sqlite3_step(update_stmt);

    sqlite3_finalize(update_stmt);

    return rc == SQLITE_DONE;
}

int db_count_unsynced(void)
{
    const char* sql =
        "SELECT COUNT(*) "
        "FROM activity_logs "
        "WHERE synced = 0;";

    sqlite3_stmt* stmt = NULL;

    if (
        sqlite3_prepare_v2(
            g_db,
            sql,
            -1,
            &stmt,
            NULL
        ) != SQLITE_OK
    )
    {
        return 0;
    }

    int count = 0;

    if (
        sqlite3_step(stmt)
        == SQLITE_ROW
    )
    {
        count =
            sqlite3_column_int(
                stmt,
                0
            );
    }

    sqlite3_finalize(stmt);

    return count;
}

int db_delete_old_records(
    int days)
{
    /*
     * QUAN TRỌNG: chỉ xóa các bản ghi ĐÃ synced=1.
     * Không bao giờ xóa dữ liệu chưa kịp đồng bộ lên
     * Supabase, để tránh mất dữ liệu vĩnh viễn.
     */
    const char* sql =
        "DELETE FROM activity_logs "
        "WHERE synced = 1 "
        "AND created_at "
        "< datetime('now', ?);";

    sqlite3_stmt* stmt = NULL;

    if (
        sqlite3_prepare_v2(
            g_db,
            sql,
            -1,
            &stmt,
            NULL
        ) != SQLITE_OK
    )
    {
        return 0;
    }

    char filter[32];

    snprintf(
        filter,
        sizeof(filter),
        "-%d day",
        days
    );

    sqlite3_bind_text(
        stmt,
        1,
        filter,
        -1,
        SQLITE_TRANSIENT
    );

    int rc =
        sqlite3_step(stmt);

    sqlite3_finalize(stmt);

    return rc == SQLITE_DONE;
}

/*
 * Xuất TOÀN BỘ bảng activity_logs (kể cả đã sync
 * lẫn chưa sync) ra một file JSON, dùng cho mục
 * đích backup cục bộ, độc lập với việc đồng bộ
 * lên Supabase.
 */
int db_export_json(
    const char* filepath)
{
    if (!g_db || !filepath)
        return 0;

    FILE* f = fopen(
        filepath,
        "wb"
    );

    if (!f)
        return 0;

    const char* sql =
        "SELECT "
        "id, device_id, process_name, window_title, "
        "duration_seconds, start_time, end_time, "
        "synced, created_at "
        "FROM activity_logs "
        "ORDER BY id;";

    sqlite3_stmt* stmt = NULL;

    if (
        sqlite3_prepare_v2(
            g_db,
            sql,
            -1,
            &stmt,
            NULL
        ) != SQLITE_OK
    )
    {
        fclose(f);
        return 0;
    }

    fprintf(f, "[\n");

    int first = 1;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (!first)
            fprintf(f, ",\n");

        first = 0;

        int id =
            sqlite3_column_int(stmt, 0);

        const char* device_id =
            (const char*)sqlite3_column_text(stmt, 1);

        const char* process_name =
            (const char*)sqlite3_column_text(stmt, 2);

        const char* window_title =
            (const char*)sqlite3_column_text(stmt, 3);

        long duration =
            (long)sqlite3_column_int64(stmt, 4);

        long long start_time =
            sqlite3_column_int64(stmt, 5);

        long long end_time =
            sqlite3_column_int64(stmt, 6);

        int synced =
            sqlite3_column_int(stmt, 7);

        const char* created_at =
            (const char*)sqlite3_column_text(stmt, 8);

        char device_esc[256]   = {0};
        char process_esc[2048] = {0};
        char title_esc[8192]   = {0};
        char created_esc[64]   = {0};

        json_escape(device_id,     device_esc,  sizeof(device_esc));
        json_escape(process_name,  process_esc, sizeof(process_esc));
        json_escape(window_title,  title_esc,   sizeof(title_esc));
        json_escape(created_at,    created_esc, sizeof(created_esc));

        fprintf(
            f,
            "  {\"id\": %d, \"device_id\": \"%s\", "
            "\"process_name\": \"%s\", \"window_title\": \"%s\", "
            "\"duration_seconds\": %ld, \"start_time\": %lld, "
            "\"end_time\": %lld, \"synced\": %d, "
            "\"created_at\": \"%s\"}",
            id,
            device_esc,
            process_esc,
            title_esc,
            duration,
            start_time,
            end_time,
            synced,
            created_esc
        );
    }

    fprintf(f, "\n]\n");

    sqlite3_finalize(stmt);
    fclose(f);

    return 1;
}

/*
 * Đánh dấu record đã sync.
 */
int db_mark_record_synced(
    int id)
{
    if (!g_db)
        return 0;

    const char* sql =
        "UPDATE activity_logs "
        "SET synced = 1 "
        "WHERE id = ?;";

    sqlite3_stmt* stmt = NULL;

    int rc =
        sqlite3_prepare_v2(
            g_db,
            sql,
            -1,
            &stmt,
            NULL
        );

    if (rc != SQLITE_OK)
        return 0;

    sqlite3_bind_int(
        stmt,
        1,
        id
    );

    rc =
        sqlite3_step(
            stmt
        );

    sqlite3_finalize(
        stmt
    );

    return rc == SQLITE_DONE;
}