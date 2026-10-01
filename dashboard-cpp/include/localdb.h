//
// localdb.h
// Cổng từ internal/localdb/localdb.go: đọc CHỈ ĐỌC (read-only) cùng
// SQLite file mà agent C++ ghi (activity_logs, xem src/shared/database.cpp)
// - nguồn dữ liệu hoạt động cả khi KHÔNG có mạng và KHÔNG đăng nhập.
//
#ifndef DASHBOARD_LOCALDB_H
#define DASHBOARD_LOCALDB_H

#include <nlohmann/json.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

struct sqlite3;

namespace jit::dash {

struct AppUsage {
    std::string processName;
    int64_t totalSeconds = 0;
};

struct Activity {
    std::string processName;
    std::string windowTitle;
    int64_t duration = 0;
    int64_t startTime = 0;
    int64_t endTime = 0;
    bool synced = false;
};

struct DayTotal {
    std::string date; // "YYYY-MM-DD"
    int64_t totalSeconds = 0;
};

class LocalDB {
public:
    // Mở CHỈ ĐỌC (mode=ro) - dashboard không bao giờ có thể làm hỏng
    // hay khoá file mà agent đang phụ thuộc vào.
    static std::unique_ptr<LocalDB> open(const std::string& pathUtf8);
    ~LocalDB();

    LocalDB(const LocalDB&) = delete;
    LocalDB& operator=(const LocalDB&) = delete;

    // Tổng số giây theo từng process trong [start, end), nhiều nhất trước, tối đa 20.
    std::vector<AppUsage> usageInRange(int64_t start, int64_t end, std::string* err = nullptr) const;

    // 1 dòng/ngày, ranh giới do caller tính theo múi giờ máy chủ.
    std::vector<DayTotal> dailyTotalsInRange(const std::vector<int64_t>& dayBoundaries, std::string* err = nullptr) const;

    // `limit` bản ghi gần nhất, mới nhất trước.
    std::vector<Activity> recentActivities(int limit, std::string* err = nullptr) const;

    // Toàn bộ bản ghi có start_time trong [dayStart, dayEnd), cũ nhất trước.
    std::vector<Activity> activitiesForDay(int64_t dayStart, int64_t dayEnd, std::string* err = nullptr) const;

private:
    explicit LocalDB(sqlite3* db) : db_(db) {}
    sqlite3* db_ = nullptr;
};

// Tên trường khớp CHÍNH XÁC json tag bên Go (internal/localdb/localdb.go) -
// dashboard.js đọc thẳng các tên này.
inline void to_json(nlohmann::json& j, const AppUsage& u)
{
    j = {{"process_name", u.processName}, {"total_seconds", u.totalSeconds}};
}
inline void to_json(nlohmann::json& j, const Activity& a)
{
    j = {{"process_name", a.processName}, {"window_title", a.windowTitle}, {"duration_seconds", a.duration},
          {"start_time", a.startTime}, {"end_time", a.endTime}, {"synced", a.synced}};
}
inline void to_json(nlohmann::json& j, const DayTotal& d)
{
    j = {{"date", d.date}, {"total_seconds", d.totalSeconds}};
}

} // namespace jit::dash

#endif
