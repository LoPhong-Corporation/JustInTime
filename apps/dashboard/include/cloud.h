//
// cloud.h
// Cổng từ internal/cloud/cloud.go: client gọi Supabase REST/Auth (GoTrue
// + PostgREST) cho toàn bộ dữ liệu "cloud" của dashboard - cùng project
// Supabase mà agent C++ và (trước đây) bản Go dùng. Dùng lại
// jit::httpsRequest (agent's httpclient.h, WinHTTP có keep-alive + timeout)
// làm tầng vận chuyển, thay vì mở lại 1 stack HTTP client thứ hai.
//
#ifndef DASHBOARD_CLOUD_H
#define DASHBOARD_CLOUD_H

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <vector>

namespace jit::dash::cloud {

using json = nlohmann::json;

// 401 từ Supabase REST - caller (server.cpp) thử refresh token 1 lần rồi gọi lại.
struct Unauthorized {};

// Lỗi xác thực THẬT (Supabase từ chối rõ ràng, có message) - khác lỗi mạng/timeout.
struct AuthError : std::runtime_error { using std::runtime_error::runtime_error; };
// Lỗi chung (REST trả lỗi khác, hoặc lỗi giao vận).
struct ApiError : std::runtime_error { using std::runtime_error::runtime_error; };

struct Session {
    std::string accessToken, refreshToken, userId, email;
};

Session login(const std::string& email, const std::string& password);           // throws AuthError
void logout(const std::string& accessToken, const std::string& scope);          // scope: "" hoặc "global"
Session refreshSession(const std::string& refreshToken);                        // throws AuthError
Session sessionFromOAuthTokens(const std::string& accessToken, const std::string& refreshToken); // throws AuthError

// Client đã gắn access token - mọi hàm dưới đây throw Unauthorized (401),
// AuthError, hoặc ApiError.
class Client {
public:
    Client(std::string accessToken) : accessToken_(std::move(accessToken)) {}

    void changePassword(const std::string& newPassword);

    struct DailyTotal { std::string deviceId, processName, day; int64_t totalSeconds; };
    std::vector<DailyTotal> dailyTotals(const std::string& dayFrom, const std::string& dayTo);

    struct RecentLog { std::string deviceId, processName, windowTitle; int64_t duration, startTime, endTime; };
    std::vector<RecentLog> recentLogs(int limit);
    std::vector<RecentLog> activityLogsForChild(const std::string& childUserId, int64_t sinceUnix, int limit);

    void ping(); // throws nếu không tới được / token không hợp lệ

    struct Message { int64_t id = 0; std::string senderDeviceId, targetDeviceId, kind, payload, createdAt; std::optional<std::string> readAt; };
    void sendMessage(const Message& msg);
    std::vector<Message> inbox(const std::string& selfDeviceId, int limit);
    std::vector<Message> thread(const std::string& selfDeviceId, const std::string& otherDeviceId, int limit);
    void markRead(int64_t id);

    struct ParentLink { int64_t id; std::string otherUserId, otherEmail, status, createdAt; std::optional<std::string> approvedAt; };
    void inviteChild(const std::string& childEmail);
    std::vector<ParentLink> listLinksAsParent();
    std::vector<ParentLink> listLinksAsChild();
    void approveLink(int64_t linkId);
    void revokeLink(int64_t linkId);

    std::vector<std::pair<int64_t, std::string>> listPermissions(); // (link id, "full"/"view_only")
    void setPermission(int64_t linkId, const std::string& level);

    struct AppLimit { int64_t id; std::string processName; std::optional<int> dailyLimitSec; bool blocked; };
    std::vector<AppLimit> listLimitsForChild(const std::string& childUserId);
    void setLimit(const std::string& childUserId, const std::string& processName, std::optional<int> dailyLimitSec, bool blocked);
    void deleteLimit(int64_t limitId);

    struct DeviceHeartbeat { std::string deviceId, hostname, lastSeen; double cpuPercent, ramPercent, diskPercent; };
    void pushHeartbeat(const std::string& deviceId, const std::string& label, double cpu, double ram, double disk);
    std::vector<DeviceHeartbeat> listHeartbeats();
    std::vector<DeviceHeartbeat> listHeartbeatsForChild(const std::string& childUserId);
    void deleteHeartbeat(const std::string& deviceId);

private:
    std::string accessToken_;
    // Gửi 1 request REST tới Supabase (path bắt đầu "/rest/v1/..." hoặc
    // "/auth/v1/..."), throw Unauthorized/AuthError/ApiError khi lỗi.
    // extraHeaders: chuỗi "Name: value\r\n" nối thêm (vd Prefer:).
    json request(const char* method, const std::string& path, const json* body = nullptr,
                 const std::wstring& extraHeaders = L"", bool expectArray = true);
};

// Trích device_id duy nhất từ danh sách daily totals (đã sắp xếp).
std::vector<std::string> distinctDevices(const std::vector<Client::DailyTotal>& totals);

// ------------------------------------------------------------------ JSON --
// Tên trường khớp CHÍNH XÁC json tag của các struct tương ứng bên Go
// (cloud.DailyTotal/RecentLog/Message/ParentLink/AppLimit/DeviceHeartbeat)
// - dashboard.js đọc thẳng các tên này.

inline void to_json(json& j, const Client::DailyTotal& t)
{
    j = {{"device_id", t.deviceId}, {"process_name", t.processName}, {"day", t.day}, {"total_seconds", t.totalSeconds}};
}

inline void to_json(json& j, const Client::RecentLog& l)
{
    j = {{"device_id", l.deviceId}, {"process_name", l.processName}, {"window_title", l.windowTitle},
          {"duration_seconds", l.duration}, {"start_time", l.startTime}, {"end_time", l.endTime}};
}

inline void to_json(json& j, const Client::Message& m)
{
    j = {{"id", m.id}, {"sender_device_id", m.senderDeviceId}, {"target_device_id", m.targetDeviceId},
          {"kind", m.kind}, {"payload", m.payload}, {"created_at", m.createdAt},
          {"read_at", m.readAt ? json(*m.readAt) : json(nullptr)}};
}

inline void to_json(json& j, const Client::ParentLink& l)
{
    j = {{"id", l.id}, {"other_user_id", l.otherUserId}, {"other_email", l.otherEmail},
          {"status", l.status}, {"created_at", l.createdAt},
          {"approved_at", l.approvedAt ? json(*l.approvedAt) : json(nullptr)}};
}

inline void to_json(json& j, const Client::AppLimit& l)
{
    j = {{"id", l.id}, {"process_name", l.processName},
          {"daily_limit_sec", l.dailyLimitSec ? json(*l.dailyLimitSec) : json(nullptr)}, {"blocked", l.blocked}};
}

inline void to_json(json& j, const Client::DeviceHeartbeat& h)
{
    j = {{"device_id", h.deviceId}, {"hostname", h.hostname}, {"cpu_percent", h.cpuPercent},
          {"ram_percent", h.ramPercent}, {"disk_percent", h.diskPercent}, {"last_seen", h.lastSeen}};
}

} // namespace jit::dash::cloud

#endif
