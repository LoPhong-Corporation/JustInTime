//
// database.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface
// extern "C" trong database.h), TOÀN BỘ câu SQL và logic nghiệp vụ
// giữ nguyên 100% so với bản C gốc trước khi chuyển đổi. Đổi cách viết:
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
#include "paths.h"
#include "strutil.h"
#include "settings.h"
#include "error_codes.h"
#include "log.h"

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

/*
 * UTF-8 -> wchar_t[wideSize], LUÔN kết thúc bằng '\0' và cắt bớt nếu quá
 * dài. Bản cũ gọi thẳng MultiByteToWideChar: khi chuỗi dài hơn buffer nó
 * trả về 0 và KHÔNG ghi '\0' => các nơi dùng wcslen()/wprintf() sau đó
 * đọc lố buffer.
 */
void utf8ToWide(const char* utf8, wchar_t* wide, int wideSize)
{
    if (!wide || wideSize <= 0)
        return;

    wide[0] = L'\0';

    if (!utf8)
        return;

    const std::wstring w = jit::utf8ToWide(utf8);
    const size_t n = w.size() < static_cast<size_t>(wideSize - 1) ? w.size() : static_cast<size_t>(wideSize - 1);

    memcpy(wide, w.data(), n * sizeof(wchar_t));
    wide[n] = L'\0';
}

const char* columnText(sqlite3_stmt* stmt, int col)
{
    const char* t = reinterpret_cast<const char*>(sqlite3_column_text(stmt, col));
    return t ? t : "";
}

/* Chạy 1 câu lệnh không có kết quả; lỗi được ghi log nhưng không dừng app. */
bool execSql(const char* sql)
{
    char* err = nullptr;
    const int rc = sqlite3_exec(g_db, sql, nullptr, nullptr, &err);

    if (rc != SQLITE_OK)
    {
        JIT_LOG(L"[DB] SQL loi (%hs): %hs\n", sql, err ? err : "?");
        sqlite3_free(err);
        return false;
    }

    return true;
}

bool tableHasColumn(const char* table, const char* column)
{
    const std::string sql = std::string("PRAGMA table_info(") + table + ");";
    Stmt stmt(g_db, sql.c_str());
    if (!stmt.ok())
        return false;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        if (strcmp(columnText(stmt, 1), column) == 0)
            return true;
    }

    return false;
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
    const std::filesystem::path dbFile = jit::configFile(L"justintime.db");
    const std::string dbPath = dbFile.empty() ? std::string("justintime.db")  /* Fallback hiếm khi xảy ra */
                                              : jit::wideToUtf8(dbFile.wstring());

    /*
     * FULLMUTEX: kết nối này được dùng từ nhiều luồng (worker ghi, GUI
     * đọc tổng kết, remote view đọc, luồng cloud đánh dấu đã sync) - buộc
     * chế độ "serialized" bất kể sqlite3.c được biên dịch với
     * SQLITE_THREADSAFE gì.
     */
    int rc = sqlite3_open_v2(
        dbPath.c_str(), &g_db,
        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX,
        nullptr);

    if (rc != SQLITE_OK)
    {
        JIT_LOG(L"[%hs] Failed to open database\n", ERR_DB_OPEN_FAIL);
        sqlite3_close(g_db);
        g_db = nullptr;
        return 0;
    }

    /*
     * QUAN TRỌNG - cùng file DB này được mở bởi 2 tiến trình: agent (ghi)
     * và dashboard-go (đọc). Với journal mặc định (DELETE), 1 lần đọc của
     * dashboard giữ khoá SHARED và làm câu INSERT của agent lỗi
     * SQLITE_BUSY NGAY LẬP TỨC (agent không đặt busy_timeout) - record
     * hoạt động bị MẤT vì db_insert_activity() chỉ trả về 0, không ai thử lại.
     *   - busy_timeout: chờ tối đa 5s thay vì lỗi ngay.
     *   - WAL: người đọc không chặn người ghi (và ngược lại).
     *   - synchronous=NORMAL: an toàn với WAL (chỉ có thể mất vài giao dịch
     *     cuối nếu mất điện đột ngột, không hỏng DB) và tránh fsync mỗi lần
     *     insert.
     */
    sqlite3_busy_timeout(g_db, 5000);
    execSql("PRAGMA journal_mode=WAL;");
    execSql("PRAGMA synchronous=NORMAL;");
    execSql("PRAGMA temp_store=MEMORY;");
    execSql("PRAGMA journal_size_limit=4194304;");

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

    if (!execSql(sql))
    {
        JIT_LOG(L"[%hs] Create table failed\n", ERR_DB_CREATE_TABLE);
        return 0;
    }

    JIT_LOG(L"Database initialized\n");

    /*
     * Migration: thêm cột cho Retry Queue nếu chưa có. Trước đây chạy
     * ALTER TABLE mù và bỏ qua lỗi "duplicate column" mỗi lần khởi động;
     * giờ kiểm tra cột trước nên lỗi thật sự (đĩa đầy, DB hỏng...) không bị
     * nuốt cùng với lỗi vô hại kia.
     */
    if (!tableHasColumn("activity_logs", "retry_count"))
        execSql("ALTER TABLE activity_logs ADD COLUMN retry_count INTEGER DEFAULT 0;");

    if (!tableHasColumn("activity_logs", "next_retry_at"))
        execSql("ALTER TABLE activity_logs ADD COLUMN next_retry_at INTEGER DEFAULT 0;");

    /*
     * Index cho các truy vấn định kỳ (phải tạo SAU migration vì
     * next_retry_at có thể mới được thêm ở trên):
     *   - idx_activity_unsynced : hàng đợi sync + db_count_unsynced().
     *     Là partial index nên chỉ chứa các dòng chưa sync (thường rất ít)
     *     dù bảng giữ 30 ngày dữ liệu.
     *   - idx_activity_proc_start : db_get_today_seconds() - chạy mỗi vài
     *     giây để kiểm tra giới hạn app - và subquery "tiêu đề gần nhất".
     *   - idx_activity_start_proc : tổng kết theo app trong ngày.
     *   - idx_activity_synced_created : db_delete_old_records().
     * Các index proc/start là covering (có cả duration_seconds) nên không
     * phải đọc bảng chính.
     */
    execSql("CREATE INDEX IF NOT EXISTS idx_activity_unsynced ON activity_logs(next_retry_at) WHERE synced = 0;");
    execSql("CREATE INDEX IF NOT EXISTS idx_activity_proc_start ON activity_logs(process_name, start_time, duration_seconds);");
    execSql("CREATE INDEX IF NOT EXISTS idx_activity_start_proc ON activity_logs(start_time, process_name, duration_seconds);");
    execSql("CREATE INDEX IF NOT EXISTS idx_activity_synced_created ON activity_logs(created_at) WHERE synced = 1;");

    // Cập nhật thống kê cho query planner (rẻ: chỉ phân tích khi cần).
    execSql("PRAGMA optimize=0x10002;");

    return 1;
}

int db_insert_activity(const ActivityRecord* record)
{
    if (!record)
        return 0;

    char deviceId[128] = {0};
    get_device_id(deviceId, sizeof(deviceId));

    // std::string: không còn buffer 512 byte cố định (tên file toàn ký tự
    // CJK dài có thể vượt 512 byte UTF-8 => trước đây thành chuỗi rỗng).
    const std::string processUtf8 = jit::wideToUtf8(record->process_name);
    const std::string titleUtf8 = jit::wideToUtf8(record->window_title);

    const char* sql =
        "INSERT INTO activity_logs ("
        "device_id,process_name,window_title,duration_seconds,start_time,end_time,synced"
        ") VALUES (?, ?, ?, ?, ?, ?, ?);";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
    {
        JIT_LOG(L"[%hs] Prepare failed: %hs\n", ERR_DB_INSERT_FAIL, sqlite3_errmsg(g_db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, deviceId, -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, processUtf8.c_str(), static_cast<int>(processUtf8.size()), SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, titleUtf8.c_str(), static_cast<int>(titleUtf8.size()), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 4, record->duration_seconds);
    sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(record->start_time));
    sqlite3_bind_int64(stmt, 6, static_cast<sqlite3_int64>(record->end_time));
    sqlite3_bind_int(stmt, 7, record->synced);

    int rc = sqlite3_step(stmt);

    if (rc != SQLITE_DONE)
    {
        JIT_LOG(L"[%hs] Insert failed: %hs\n", ERR_DB_INSERT_FAIL, sqlite3_errmsg(g_db));
        return 0;
    }

    JIT_LOG(L"Activity saved\n");
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

    const std::string processUtf8 = jit::wideToUtf8(process_name);

    sqlite3_bind_text(stmt, 1, processUtf8.c_str(), static_cast<int>(processUtf8.size()), SQLITE_TRANSIENT);
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
    // Chỉ để in ra console debug - không tốn 1 truy vấn DB (mỗi 5 phút)
    // chỉ để ghi vào 1 console đang bị ẩn.
    if (!jit_log_enabled())
        return;

    wchar_t buffer[4096] = {0};
    db_build_daily_summary_text(buffer, 4096);

    JIT_LOG(
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
        execSql("PRAGMA optimize;");
        sqlite3_close(g_db);
        g_db = nullptr;
    }
}

long long db_change_counter(void)
{
    return g_db ? static_cast<long long>(sqlite3_total_changes64(g_db)) : 0;
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

        JIT_LOG(L"[UNSYNCED] id=%d process=%ls duration=%d\n", id, process, duration);
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
        "ORDER BY next_retry_at ASC, id ASC "
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

        // columnText(): sqlite3_column_text() có thể trả NULL, và truyền
        // NULL cho "%s" là hành vi không xác định.
        snprintf(rec->device_id, sizeof(rec->device_id), "%s", columnText(stmt, 1));

        utf8ToWide(columnText(stmt, 2), rec->process_name, 512);
        utf8ToWide(columnText(stmt, 3), rec->window_title, 2048);

        rec->duration_seconds = sqlite3_column_int64(stmt, 4);
        rec->start_time = sqlite3_column_int64(stmt, 5);
        rec->end_time = sqlite3_column_int64(stmt, 6);

        count++;
    }

    return count;
}

int db_mark_synced(int id)
{
    if (!g_db)
        return 0;

    Stmt stmt(g_db, "UPDATE activity_logs SET synced = 1 WHERE id = ?;");
    if (!stmt.ok())
        return 0;

    sqlite3_bind_int(stmt, 1, id);

    return sqlite3_step(stmt) == SQLITE_DONE;
}

int db_mark_synced_batch(const int* ids, int count)
{
    if (!g_db || !ids || count <= 0)
        return 0;

    /*
     * 1 câu UPDATE ... WHERE id IN (...) là atomic và chỉ tốn 1 lần commit.
     * Chỉ nối SỐ NGUYÊN vào câu lệnh (không có chuỗi người dùng nào) nên
     * không có rủi ro SQL injection. Chia đoạn 500 id để câu SQL luôn ngắn.
     */
    constexpr int kChunk = 500;
    int ok = 1;

    for (int offset = 0; offset < count; offset += kChunk)
    {
        const int n = (count - offset) < kChunk ? (count - offset) : kChunk;

        std::string sql = "UPDATE activity_logs SET synced = 1 WHERE id IN (";
        for (int i = 0; i < n; i++)
        {
            if (i > 0)
                sql += ',';
            sql += std::to_string(ids[offset + i]);
        }
        sql += ");";

        if (!execSql(sql.c_str()))
            ok = 0;
    }

    return ok;
}

/*
 * Đánh dấu 1 record vừa gửi lên cloud thất bại: tăng retry_count và
 * tính lại next_retry_at theo kiểu exponential backoff (base * 2^retry_count,
 * giới hạn bởi max), để không spam server liên tục khi 1 record cứ bị từ
 * chối, đồng thời tự thử lại nhanh hơn khi vừa lỗi.
 *
 * Trước đây làm bằng SELECT retry_count -> tính trong C++ -> UPDATE (2 câu
 * lệnh, và không atomic). Giờ là 1 câu UPDATE duy nhất; công thức giữ
 * nguyên (mũ giới hạn ở 20 để tránh tràn số) - đã đối chiếu với công thức
 * C++ cũ trên nhiều cặp (retry_count, base, max).
 */
int db_mark_sync_failed(int id)
{
    if (!g_db)
        return 0;

    AppSettings s;
    settings_get(&s);

    const char* sql =
        "UPDATE activity_logs "
        "SET retry_count = COALESCE(retry_count, 0) + 1, "
        "    next_retry_at = CAST(strftime('%s','now') AS INTEGER) "
        "                    + MAX(1, MIN(?1, ?2 << MIN(COALESCE(retry_count, 0) + 1, 20))) "
        "WHERE id = ?3;";

    Stmt stmt(g_db, sql);
    if (!stmt.ok())
        return 0;

    sqlite3_bind_int64(stmt, 1, s.retry_backoff_max_sec);
    sqlite3_bind_int64(stmt, 2, s.retry_backoff_base_sec);
    sqlite3_bind_int(stmt, 3, id);

    return sqlite3_step(stmt) == SQLITE_DONE;
}

int db_count_unsynced(void)
{
    if (!g_db)
        return 0;

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
    if (!g_db)
        return 0;

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
 *
 * So với bản cũ:
 *   - Escape bằng std::string (jit::jsonEscape) thay vì 4 buffer cố định
 *     - buffer title 8192 byte có thể cắt cụt ở trường hợp xấu nhất
 *     (json_escape nở tối đa 6 lần).
 *   - Có buffer ghi 64KB (mặc định của stdio là 4KB).
 *   - Ghi ra "<file>.tmp" rồi MoveFileEx: crash giữa chừng không để lại
 *     file backup cụt.
 *   - Đường dẫn UTF-8 -> _wfopen (chạy đúng với tên thư mục Unicode).
 *   - Dưới WAL, việc đọc toàn bảng ở đây không chặn việc insert của
 *     luồng theo dõi.
 */
int db_export_json(const char* filepath)
{
    if (!g_db || !filepath)
        return 0;

    const std::wstring target = jit::utf8ToWide(filepath);
    if (target.empty())
        return 0;

    const std::wstring tmp = target + L".tmp";

    FILE* f = _wfopen(tmp.c_str(), L"wb");
    if (!f)
        return 0;

    setvbuf(f, nullptr, _IOFBF, 1 << 16);

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
        DeleteFileW(tmp.c_str());
        return 0;
    }

    fputs("[\n", f);

    bool first = true;
    std::string row;

    while (sqlite3_step(stmt) == SQLITE_ROW)
    {
        row.clear();
        row += first ? "  {" : ",\n  {";
        first = false;

        row += "\"id\": " + std::to_string(sqlite3_column_int(stmt, 0));
        row += ", \"device_id\": \"" + jit::jsonEscape(columnText(stmt, 1)) + "\"";
        row += ", \"process_name\": \"" + jit::jsonEscape(columnText(stmt, 2)) + "\"";
        row += ", \"window_title\": \"" + jit::jsonEscape(columnText(stmt, 3)) + "\"";
        row += ", \"duration_seconds\": " + std::to_string(static_cast<long long>(sqlite3_column_int64(stmt, 4)));
        row += ", \"start_time\": " + std::to_string(static_cast<long long>(sqlite3_column_int64(stmt, 5)));
        row += ", \"end_time\": " + std::to_string(static_cast<long long>(sqlite3_column_int64(stmt, 6)));
        row += ", \"synced\": " + std::to_string(sqlite3_column_int(stmt, 7));
        row += ", \"created_at\": \"" + jit::jsonEscape(columnText(stmt, 8)) + "\"}";

        fwrite(row.data(), 1, row.size(), f);
    }

    fputs("\n]\n", f);

    const bool wroteOk = (fflush(f) == 0) && (ferror(f) == 0);
    fclose(f);

    if (!wroteOk ||
        !MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(tmp.c_str());
        return 0;
    }

    return 1;
}

/*
 * Đánh dấu record đã sync.
 */
int db_mark_record_synced(int id)
{
    return db_mark_synced(id);
}
