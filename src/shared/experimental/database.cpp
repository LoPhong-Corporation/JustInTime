//
// database.cpp
//
// CHUYỂN TỪ C SANG C++ (bản EXPERIMENTAL) - giữ nguyên interface
// extern "C" trong database.h, TOÀN BỘ câu SQL và logic nghiệp vụ
// giữ nguyên 100% so với bản STABLE. Đổi cách viết:
//   - RAII cho sqlite3_stmt* (lớp Stmt bên dưới) - tự gọi
//     sqlite3_finalize() khi ra khỏi scope, ở MỌI nhánh return (kể
//     cả early-return khi prepare lỗi). Bản C cũ có rất nhiều hàm
//     lặp lại chuỗi "prepare -> (lỗi thì return 0) -> bind -> step ->
//     finalize -> return" - dễ sai nếu 1 nhánh return mới được thêm
//     vào sau này mà quên finalize. RAII loại bỏ hẳn khả năng đó.
//   - std::string cho vài chỗ build chuỗi động (device_id escape,
//     filter ngày tháng...).
//   - Vẫn dùng thẳng C API của sqlite3 (thư viện third-party, không
//     đổi) - chỉ bọc phần quản lý vòng đời statement.
//

#include "database.h"
#include "device.h"
#include "jsonutil.h"
#include "settings.h"
#include "error_codes.h"

#include "sqlite/sqlite3.h"

#include <windows.h>

#include <cstdio>
#include <ctime>
#include <cstring>
#include <string>

namespace {

sqlite3* g_db = nullptr;

/*
 * RAII cho sqlite3_stmt* - tự sqlite3_finalize() khi ra khỏi scope.
 * Không cho copy (1 statement chỉ có 1 chủ sở hữu).
 */
class Stmt
{
public:
    Stmt(sqlite3* db, const char* sql)
    {
        sqlite3_prepare_v2(db, sql, -1, &m_stmt, nullptr);
    }

    ~Stmt()
    {
        if (m_stmt)
            sqlite3_finalize(m_stmt);
    }

    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    bool ok() const { return m_stmt != nullptr; }
    sqlite3_stmt* get() const { return m_stmt; }
    operator sqlite3_stmt*() const { return m_stmt; }

private:
    sqlite3_stmt* m_stmt = nullptr;
};

void wideToUtf8(const wchar_t* wide, char* utf8, int utf8Size)
{
    WideCharToMultiByte(CP_UTF8, 0, wide, -1, utf8, utf8Size, NULL, NULL);
}

void utf8ToWide(const char* utf8, wchar_t* wide, int wideSize)
{
    if (!utf8)
    {
        wide[0] = L'\0';
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, wideSize);
}

/* 00:00 giờ địa phương hôm nay, dùng chung cho mọi truy vấn "hôm nay". */
time_t todayStart()
{
    time_t now = time(NULL);
    struct tm today;
    localtime_s(&today, &now);
    today.tm_hour = 0;
    today.tm_min = 0;
    today.tm_sec = 0;
    return mktime(&today);
}

} // namespace

int db_init(void)
{
    char dir[MAX_PATH];
    char dbPath[MAX_PATH];

    if (settings_get_config_dir(dir, sizeof(dir)))
        snprintf(dbPath, sizeof(dbPath), "%s\\justintime.db", dir);
    else
        snprintf(dbPath, sizeof(dbPath), "justintime.db"); /* Fallback hiếm khi xảy ra */

    int rc = sqlite3_open(dbPath, &g_db);

    if (rc != SQLITE_OK)
    {
        wprintf(L"[%hs] Failed to open database\n", ERR_DB_OPEN_FAIL);
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

    char* error = nullptr;
    rc = sqlite3_exec(g_db, sql, nullptr, nullptr, &error);

    if (rc != SQLITE_OK)
    {
        wprintf(L"[%hs] Create table failed: %hs\n", ERR_DB_CREATE_TABLE, error);
        sqlite3_free(error);
        return 0;
    }

    wprintf(L"Database initialized\n");

    /*
     * Migration: thêm cột cho Retry Queue nếu chưa có (ALTER TABLE
     * ADD COLUMN lỗi vô hại nếu cột đã tồn tại, nên cố tình bỏ qua
     * kết quả trả về).
     */
    sqlite3_exec(g_db, "ALTER TABLE activity_logs ADD COLUMN retry_count INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);
    sqlite3_exec(g_db, "ALTER TABLE activity_logs ADD COLUMN next_retry_at INTEGER DEFAULT 0;", nullptr, nullptr, nullptr);

    return 1;
}

int db_insert_activity(const ActivityRecord* record)
{
    if (!record)
        return 0;

    char deviceId[128] = {0};
    get_device_id(deviceId, sizeof(deviceId));

    char processUtf8[512] = {0};
    char titleUtf8[2048] = {0};
    wideToUtf8(record->process_name, processUtf8, sizeof(processUtf8));
    wideToUtf8(record->window_title, titleUtf8, sizeof(titleUtf8));

    const char* sql =
        "INSERT INTO activity_logs ("
        "device_id,process_name,window_title,duration_seconds,start_time,end_time,synced"
        ") VALUES (?, ?, ?, ?, ?, ?, ?);";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
    {
        wprintf(L"[%hs] Prepare failed: %hs\n", ERR_DB_INSERT_FAIL, sqlite3_errmsg(g_db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, deviceId, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, processUtf8, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, titleUtf8, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, record->duration_seconds);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(record->start_time));
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(record->end_time));
    sqlite3_bind_int(stmt, 7, record->synced);

    int rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE)
    {
        wprintf(L"[%hs] Insert failed: %hs\n", ERR_DB_INSERT_FAIL, sqlite3_errmsg(g_db));
        return 0;
    }

    wprintf(L"Activity saved\n");
    return 1;
}

/*
 * Build noi dung bao cao tong thoi gian su dung moi app hom nay vao
 * buffer (dung chung cho console lan tray MessageBox). Tra ve 1 neu
 * co du lieu, 0 neu chua co.
 */
int db_build_daily_summary_text(wchar_t* out, int out_size)
{
    out[0] = L'\0';

    if (!g_db)
        return 0;

    const time_t dayStart = todayStart();

    const char* sql =
        "SELECT a1.process_name, SUM(a1.duration_seconds) as total, "
        "  (SELECT a2.window_title FROM activity_logs a2 "
        "   WHERE a2.process_name = a1.process_name AND a2.start_time >= ? "
        "   ORDER BY a2.start_time DESC LIMIT 1) as last_title "
        "FROM activity_logs a1 "
        "WHERE a1.start_time >= ? "
        "GROUP BY a1.process_name "
        "ORDER BY total DESC "
        "LIMIT 15;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return 0;

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(dayStart));
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(dayStart));

    int hasRow = 0;
    int pos = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        hasRow = 1;

        const char* processUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        long long totalSeconds = sqlite3_column_int64(stmt, 1);
        const char* titleUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));

        wchar_t processWide[512] = {0};
        utf8ToWide(processUtf8, processWide, 512);

        /*
         * Tiêu đề cửa sổ gần nhất của process này hôm nay (có thể
         * NULL nếu record cũ chưa lưu window_title) - cắt bớt nếu
         * quá dài để dòng báo cáo không bị vỡ layout.
         */
        wchar_t titleWide[512] = {0};
        utf8ToWide(titleUtf8, titleWide, 512);

        if (wcslen(titleWide) > 50)
        {
            titleWide[47] = L'.';
            titleWide[48] = L'.';
            titleWide[49] = L'.';
            titleWide[50] = L'\0';
        }

        long hours = static_cast<long>(totalSeconds / 3600);
        long minutes = static_cast<long>((totalSeconds % 3600) / 60);
        long seconds = static_cast<long>(totalSeconds % 60);

        int written;

        if (titleWide[0] != L'\0')
        {
            written = swprintf(
                out + pos, out_size - pos,
                L"%-28ls %02ld:%02ld:%02ld  - %ls\n",
                processWide, hours, minutes, seconds, titleWide
            );
        }
        else
        {
            written = swprintf(
                out + pos, out_size - pos,
                L"%-28ls %02ld:%02ld:%02ld\n",
                processWide, hours, minutes, seconds
            );
        }

        if (written < 0)
            break;

        pos += written;
    }

    if (!hasRow)
        swprintf(out, out_size, L"(No data yet today)");

    return hasRow;
}

int db_get_today_app_summary(DailyAppSummaryEntry* out, int max_entries)
{
    if (!g_db || !out || max_entries <= 0)
        return 0;

    if (max_entries > DAILY_APP_SUMMARY_MAX)
        max_entries = DAILY_APP_SUMMARY_MAX;

    const time_t dayStart = todayStart();

    /* Cùng logic gộp/sắp xếp với db_build_daily_summary_text(), chỉ
     * bỏ subquery window_title (không cần cho danh sách icon trên
     * Overview). */
    const char* sql =
        "SELECT process_name, SUM(duration_seconds) as total "
        "FROM activity_logs "
        "WHERE start_time >= ? "
        "GROUP BY process_name "
        "ORDER BY total DESC "
        "LIMIT ?;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return 0;

    sqlite3_bind_int64(stmt, 1, static_cast<sqlite3_int64>(dayStart));
    sqlite3_bind_int(stmt, 2, max_entries);

    int count = 0;

    while (count < max_entries && sqlite3_step(stmt) == SQLITE_ROW)
    {
        const char* processUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        long long totalSeconds = sqlite3_column_int64(stmt, 1);

        utf8ToWide(processUtf8, out[count].process_name, 512);
        out[count].total_seconds = totalSeconds;
        count++;
    }

    return count;
}

long db_get_today_seconds(const wchar_t* process_name)
{
    if (!g_db || !process_name)
        return 0;

    const time_t dayStart = todayStart();

    const char* sql =
        "SELECT COALESCE(SUM(duration_seconds), 0) "
        "FROM activity_logs "
        "WHERE process_name = ? AND start_time >= ?;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return 0;

    char processUtf8[512] = {0};
    wideToUtf8(process_name, processUtf8, sizeof(processUtf8));

    sqlite3_bind_text(stmt, 1, processUtf8, -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, static_cast<sqlite3_int64>(dayStart));

    long total = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        total = static_cast<long>(sqlite3_column_int64(stmt, 0));

    return total;
}

/*
 * In bao cao tong thoi gian su dung moi app trong ngay hom nay ra
 * console (gop tat ca record du bi chia nho theo tung lan doi window
 * title). Khong thay doi cach luu chi tiet tung record, chi tong
 * hop luc hien thi.
 */
void db_print_daily_summary(void)
{
    wchar_t buffer[4096] = {0};
    db_build_daily_summary_text(buffer, 4096);

    wprintf(
        L"\n"
        L"========== TONG KET HOM NAY (theo app) ==========\n"
        L"%ls"
        L"==================================================\n",
        buffer
    );
}

void db_close(void)
{
    if (g_db)
    {
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

void db_print_unsynced(void)
{
    if (!g_db)
        return;

    const char* sql =
        "SELECT id, process_name, duration_seconds "
        "FROM activity_logs "
        "WHERE synced = 0;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        int id = sqlite3_column_int(stmt, 0);
        const char* processUtf8 = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));

        wchar_t process[512];
        utf8ToWide(processUtf8, process, 512);

        int duration = sqlite3_column_int(stmt, 2);

        wprintf(L"[UNSYNCED] id=%d process=%ls duration=%d\n", id, process, duration);
    }
}

int db_get_unsynced_records(SyncRecord* records, int max_records)
{
    if (!g_db)
        return 0;

    const char* sql =
        "SELECT id,device_id,process_name,window_title,duration_seconds,start_time,end_time "
        "FROM activity_logs "
        "WHERE synced = 0 "
        "AND next_retry_at <= CAST(strftime('%s','now') AS INTEGER) "
        "ORDER BY next_retry_at ASC "
        "LIMIT ?;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return 0;

    sqlite3_bind_int(stmt, 1, max_records);

    int count = 0;

    while (sqlite3_step(stmt) == SQLITE_ROW && count < max_records)
    {
        SyncRecord* rec = &records[count];

        rec->id = sqlite3_column_int(stmt, 0);

        snprintf(
            rec->device_id, sizeof(rec->device_id), "%s",
            reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1))
        );

        utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2)), rec->process_name, 512);
        utf8ToWide(reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3)), rec->window_title, 2048);

        rec->duration_seconds = sqlite3_column_int64(stmt, 4);
        rec->start_time = sqlite3_column_int64(stmt, 5);
        rec->end_time = sqlite3_column_int64(stmt, 6);

        count++;
    }

    return count;
}

int db_mark_synced(int id)
{
    const char* sql = "UPDATE activity_logs SET synced = 1 WHERE id = ?;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return 0;

    sqlite3_bind_int(stmt, 1, id);

    return sqlite3_step(stmt) == SQLITE_DONE;
}

/*
 * Đánh dấu 1 record vừa gửi lên cloud thất bại: tăng retry_count và
 * tính lại next_retry_at theo kiểu exponential backoff (2^retry_count
 * * base, giới hạn bởi max), để không spam server liên tục khi mất
 * mạng dài ngày, đồng thời tự thử lại nhanh hơn khi vừa lỗi.
 */
int db_mark_sync_failed(int id)
{
    if (!g_db)
        return 0;

    AppSettings s;
    settings_get(&s);

    int retryCount = 0;
    {
        Stmt selectStmt(g_db, "SELECT retry_count FROM activity_logs WHERE id = ?;");
        if (selectStmt.ok())
        {
            sqlite3_bind_int(selectStmt, 1, id);
            if (sqlite3_step(selectStmt) == SQLITE_ROW)
                retryCount = sqlite3_column_int(selectStmt, 0);
        }
    }

    retryCount++;

    /*
     * backoff = base * 2^retry_count, giới hạn bởi max. Giới hạn
     * retry_count dùng để tính lũy thừa ở 20 để tránh tràn số nếu
     * retry_count quá lớn theo thời gian.
     */
    int expCap = retryCount > 20 ? 20 : retryCount;
    long long backoff = static_cast<long long>(s.retry_backoff_base_sec) << expCap;

    if (backoff > s.retry_backoff_max_sec || backoff <= 0)
        backoff = s.retry_backoff_max_sec;

    const char* updateSql =
        "UPDATE activity_logs "
        "SET retry_count = ?, "
        "next_retry_at = CAST(strftime('%s','now') AS INTEGER) + ? "
        "WHERE id = ?;";

    Stmt updateStmt(g_db, updateSql);
    if (!updateStmt.ok())
        return 0;

    sqlite3_bind_int(updateStmt, 1, retryCount);
    sqlite3_bind_int64(updateStmt, 2, backoff);
    sqlite3_bind_int(updateStmt, 3, id);

    return sqlite3_step(updateStmt) == SQLITE_DONE;
}

int db_count_unsynced(void)
{
    const char* sql = "SELECT COUNT(*) FROM activity_logs WHERE synced = 0;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return 0;

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW)
        count = sqlite3_column_int(stmt, 0);

    return count;
}

int db_delete_old_records(int days)
{
    /*
     * QUAN TRỌNG: chỉ xóa các bản ghi ĐÃ synced=1. Không bao giờ xóa
     * dữ liệu chưa kịp đồng bộ lên Supabase, để tránh mất dữ liệu
     * vĩnh viễn.
     */
    const char* sql =
        "DELETE FROM activity_logs "
        "WHERE synced = 1 "
        "AND created_at < datetime('now', ?);";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return 0;

    char filter[32];
    snprintf(filter, sizeof(filter), "-%d day", days);

    sqlite3_bind_text(stmt, 1, filter, -1, SQLITE_TRANSIENT);

    return sqlite3_step(stmt) == SQLITE_DONE;
}

/*
 * Xuất TOÀN BỘ bảng activity_logs (kể cả đã sync lẫn chưa sync) ra
 * một file JSON, dùng cho mục đích backup cục bộ, độc lập với việc
 * đồng bộ lên Supabase.
 */
int db_export_json(const char* filepath)
{
    if (!g_db || !filepath)
        return 0;

    FILE* f = fopen(filepath, "wb");
    if (!f)
        return 0;

    const char* sql =
        "SELECT id, device_id, process_name, window_title, "
        "duration_seconds, start_time, end_time, "
        "synced, created_at "
        "FROM activity_logs "
        "ORDER BY id;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
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

        int id = sqlite3_column_int(stmt, 0);
        const char* deviceId = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        const char* processName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        const char* windowTitle = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        long duration = static_cast<long>(sqlite3_column_int64(stmt, 4));
        long long startTime = sqlite3_column_int64(stmt, 5);
        long long endTime = sqlite3_column_int64(stmt, 6);
        int synced = sqlite3_column_int(stmt, 7);
        const char* createdAt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 8));

        char deviceEsc[256] = {0};
        char processEsc[2048] = {0};
        char titleEsc[8192] = {0};
        char createdEsc[64] = {0};

        json_escape(deviceId, deviceEsc, sizeof(deviceEsc));
        json_escape(processName, processEsc, sizeof(processEsc));
        json_escape(windowTitle, titleEsc, sizeof(titleEsc));
        json_escape(createdAt, createdEsc, sizeof(createdEsc));

        fprintf(
            f,
            "  {\"id\": %d, \"device_id\": \"%s\", "
            "\"process_name\": \"%s\", \"window_title\": \"%s\", "
            "\"duration_seconds\": %ld, \"start_time\": %lld, "
            "\"end_time\": %lld, \"synced\": %d, "
            "\"created_at\": \"%s\"}",
            id, deviceEsc, processEsc, titleEsc, duration, startTime, endTime, synced, createdEsc
        );
    }

    fprintf(f, "\n]\n");
    fclose(f);

    return 1;
}

/*
 * Đánh dấu record đã sync.
 */
int db_mark_record_synced(int id)
{
    if (!g_db)
        return 0;

    const char* sql = "UPDATE activity_logs SET synced = 1 WHERE id = ?;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return 0;

    sqlite3_bind_int(stmt, 1, id);

    return sqlite3_step(stmt) == SQLITE_DONE;
}
