#include "localdb.h"
#include "sqlite/sqlite3.h"

#include <memory>

namespace jit::dash {

std::unique_ptr<LocalDB> LocalDB::open(const std::string& pathUtf8)
{
    sqlite3* db = nullptr;

    // Chỉ mở đọc (SQLITE_OPEN_READONLY) - không bao giờ tạo file mới, không
    // bao giờ ghi. busy_timeout để chịu được lúc agent đang ghi dưới WAL
    // (đọc không bị chặn bởi WAL, nhưng vẫn đặt cho chắc/tương thích ngược).
    const int rc = sqlite3_open_v2(pathUtf8.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
    if (rc != SQLITE_OK)
    {
        if (db)
            sqlite3_close(db);
        return nullptr;
    }
    sqlite3_busy_timeout(db, 3000);

    return std::unique_ptr<LocalDB>(new LocalDB(db));
}

LocalDB::~LocalDB()
{
    if (db_)
        sqlite3_close(db_);
}

namespace {

struct Stmt {
    sqlite3_stmt* p = nullptr;
    Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &p, nullptr); }
    ~Stmt() { sqlite3_finalize(p); }
    operator sqlite3_stmt*() const { return p; }
    bool ok() const { return p != nullptr; }
};

const char* text(sqlite3_stmt* s, int col)
{
    const char* t = reinterpret_cast<const char*>(sqlite3_column_text(s, col));
    return t ? t : "";
}

void setErr(std::string* err, sqlite3* db, const char* fallback)
{
    if (err)
        *err = db ? sqlite3_errmsg(db) : fallback;
}

} // namespace

std::vector<AppUsage> LocalDB::usageInRange(int64_t start, int64_t end, std::string* err) const
{
    std::vector<AppUsage> out;
    if (!db_) { setErr(err, nullptr, "local database not available"); return out; }

    Stmt s(db_, "SELECT process_name, SUM(duration_seconds) AS total "
                "FROM activity_logs WHERE start_time >= ?1 AND start_time < ?2 "
                "GROUP BY process_name ORDER BY total DESC LIMIT 20;");
    if (!s.ok()) { setErr(err, db_, ""); return out; }

    sqlite3_bind_int64(s, 1, start);
    sqlite3_bind_int64(s, 2, end);

    while (sqlite3_step(s) == SQLITE_ROW)
        out.push_back({text(s, 0), sqlite3_column_int64(s, 1)});

    return out;
}

std::vector<DayTotal> LocalDB::dailyTotalsInRange(const std::vector<int64_t>& b, std::string* err) const
{
    std::vector<DayTotal> out;
    if (!db_) { setErr(err, nullptr, "local database not available"); return out; }
    if (b.size() < 2)
        return out;

    out.reserve(b.size() - 1);

    for (size_t i = 0; i + 1 < b.size(); i++)
    {
        Stmt s(db_, "SELECT SUM(duration_seconds) FROM activity_logs WHERE start_time >= ?1 AND start_time < ?2;");
        if (!s.ok()) { setErr(err, db_, ""); return out; }

        sqlite3_bind_int64(s, 1, b[i]);
        sqlite3_bind_int64(s, 2, b[i + 1]);

        int64_t total = 0;
        if (sqlite3_step(s) == SQLITE_ROW)
            total = sqlite3_column_type(s, 0) == SQLITE_NULL ? 0 : sqlite3_column_int64(s, 0);

        time_t t = static_cast<time_t>(b[i]);
        struct tm tmBuf;
#ifdef _WIN32
        localtime_s(&tmBuf, &t);
#else
        localtime_r(&t, &tmBuf);
#endif
        char buf[16];
        strftime(buf, sizeof(buf), "%Y-%m-%d", &tmBuf);

        out.push_back({buf, total});
    }

    return out;
}

std::vector<Activity> LocalDB::recentActivities(int limit, std::string* err) const
{
    std::vector<Activity> out;
    if (!db_) { setErr(err, nullptr, "local database not available"); return out; }

    Stmt s(db_, "SELECT process_name, window_title, duration_seconds, start_time, end_time, synced "
                "FROM activity_logs ORDER BY start_time DESC LIMIT ?1;");
    if (!s.ok()) { setErr(err, db_, ""); return out; }

    sqlite3_bind_int(s, 1, limit);

    while (sqlite3_step(s) == SQLITE_ROW)
    {
        out.push_back({
            text(s, 0), text(s, 1),
            sqlite3_column_int64(s, 2), sqlite3_column_int64(s, 3), sqlite3_column_int64(s, 4),
            sqlite3_column_int(s, 5) != 0
        });
    }

    return out;
}

std::vector<Activity> LocalDB::activitiesForDay(int64_t dayStart, int64_t dayEnd, std::string* err) const
{
    std::vector<Activity> out;
    if (!db_) { setErr(err, nullptr, "local database not available"); return out; }

    Stmt s(db_, "SELECT process_name, window_title, duration_seconds, start_time, end_time, synced "
                "FROM activity_logs WHERE start_time >= ?1 AND start_time < ?2 ORDER BY start_time ASC;");
    if (!s.ok()) { setErr(err, db_, ""); return out; }

    sqlite3_bind_int64(s, 1, dayStart);
    sqlite3_bind_int64(s, 2, dayEnd);

    while (sqlite3_step(s) == SQLITE_ROW)
    {
        out.push_back({
            text(s, 0), text(s, 1),
            sqlite3_column_int64(s, 2), sqlite3_column_int64(s, 3), sqlite3_column_int64(s, 4),
            sqlite3_column_int(s, 5) != 0
        });
    }

    return out;
}

} // namespace jit::dash
