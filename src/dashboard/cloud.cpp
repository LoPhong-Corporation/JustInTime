#include "cloud.h"
#include "dashconfig.h"
#include "httpclient.h"
#include "strutil.h"

#include <algorithm>
#include <sstream>

namespace jit::dash::cloud {

namespace {

std::string urlEncode(const std::string& s)
{
    std::ostringstream out;
    for (unsigned char c : s)
    {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
            out << c;
        else
            out << '%' << std::uppercase << std::hex << static_cast<int>(c) << std::nouppercase << std::dec;
    }
    return out.str();
}

const std::wstring kHost = jit::utf8ToWide(jit::hostFromUrl(kSupabaseURL));
const std::wstring kApikeyHeader = L"apikey: " + jit::utf8ToWide(kSupabaseAnonKey) + L"\r\n";

// Đọc {"error_description"|"msg"|"message": "..."} từ phản hồi GoTrue lỗi -
// giống decodeAuthError() bên Go.
std::string parseAuthErrorMessage(const std::string& body, DWORD status)
{
    try
    {
        const auto data = nlohmann::json::parse(body);
        for (const char* key : {"error_description", "msg", "message"})
            if (data.contains(key) && data[key].is_string() && !data[key].get<std::string>().empty())
                return data[key];
    }
    catch (...) {}
    return "unexpected error (HTTP " + std::to_string(status) + ")";
}

Session parseSessionResponse(const std::string& body)
{
    const auto raw = nlohmann::json::parse(body);
    Session s;
    s.accessToken = raw.value("access_token", "");
    s.refreshToken = raw.value("refresh_token", "");
    if (raw.contains("user") && raw["user"].is_object())
    {
        s.userId = raw["user"].value("id", "");
        s.email = raw["user"].value("email", "");
    }
    return s;
}

// POST /auth/v1/token?grant_type=... hoặc GET /auth/v1/user, KHÔNG kèm
// Authorization của 1 Client (dùng thẳng apikey) - cho Login/RefreshSession.
jit::HttpResult authRequest(const wchar_t* method, const std::wstring& path, const std::string& body,
                             const std::wstring& extraHeaders = L"")
{
    jit::HttpResult result;
    std::wstring headers = kApikeyHeader + L"Content-Type: application/json\r\n" + extraHeaders;
    if (!jit::httpsRequest(kHost, method, path, headers, body, result, method == std::wstring(L"GET")))
        throw ApiError("network error contacting Supabase");
    return result;
}

} // namespace

Session login(const std::string& email, const std::string& password)
{
    nlohmann::json body = {{"email", email}, {"password", password}};
    const jit::HttpResult r = authRequest(L"POST", L"/auth/v1/token?grant_type=password", body.dump());
    if (r.status != 200)
        throw AuthError(parseAuthErrorMessage(r.body, r.status));
    return parseSessionResponse(r.body);
}

void logout(const std::string& accessToken, const std::string& scope)
{
    std::wstring path = L"/auth/v1/logout";
    if (!scope.empty())
        path += L"?scope=" + jit::utf8ToWide(scope);

    const std::wstring headers = L"Authorization: Bearer " + jit::utf8ToWide(accessToken) + L"\r\n";
    const jit::HttpResult r = authRequest(L"POST", path, "", headers);

    // GoTrue trả 204 khi thành công; 401 nghĩa là token đã hết hạn/thu hồi
    // từ trước - coi như đã "logout" rồi, không phải lỗi thật.
    if (r.status != 204 && r.status != 401)
        throw AuthError(parseAuthErrorMessage(r.body, r.status));
}

Session refreshSession(const std::string& refreshToken)
{
    nlohmann::json body = {{"refresh_token", refreshToken}};
    const jit::HttpResult r = authRequest(L"POST", L"/auth/v1/token?grant_type=refresh_token", body.dump());
    if (r.status != 200)
        throw AuthError(parseAuthErrorMessage(r.body, r.status));
    return parseSessionResponse(r.body);
}

Session sessionFromOAuthTokens(const std::string& accessToken, const std::string& refreshToken)
{
    if (accessToken.empty() || refreshToken.empty())
        throw AuthError("missing access_token or refresh_token");

    const std::wstring headers = L"Authorization: Bearer " + jit::utf8ToWide(accessToken) + L"\r\n";
    const jit::HttpResult r = authRequest(L"GET", L"/auth/v1/user", "", headers);
    if (r.status != 200)
        throw AuthError(parseAuthErrorMessage(r.body, r.status));

    const auto user = nlohmann::json::parse(r.body);
    const std::string email = user.value("email", "");
    if (email.empty())
        throw AuthError("OAuth provider did not return an email address");

    Session s;
    s.accessToken = accessToken;
    s.refreshToken = refreshToken;
    s.userId = user.value("id", "");
    s.email = email;
    return s;
}

// ---------------------------------------------------------------- Client --

json Client::request(const char* method, const std::string& path, const json* body,
                      const std::wstring& extraHeaders, bool expectArray)
{
    std::wstring headers = kApikeyHeader + L"Content-Type: application/json\r\nAuthorization: Bearer " +
                            jit::utf8ToWide(accessToken_) + L"\r\n" + extraHeaders;

    const std::string bodyStr = body ? body->dump() : "";
    const std::wstring methodW = jit::utf8ToWide(method);
    const bool idempotent = (methodW == L"GET");

    jit::HttpResult r;
    if (!jit::httpsRequest(kHost, methodW.c_str(), jit::utf8ToWide(path), headers, bodyStr, r, idempotent))
        throw ApiError("network error contacting Supabase");

    if (r.status == 401)
        throw Unauthorized{};
    if (r.status < 200 || r.status >= 300)
        throw ApiError("supabase returned " + std::to_string(r.status) + ": " + r.body);

    if (r.body.empty())
        return expectArray ? json::array() : json::object();

    try { return json::parse(r.body); }
    catch (const std::exception& e) { throw ApiError(std::string("invalid JSON from supabase: ") + e.what()); }
}

void Client::changePassword(const std::string& newPassword)
{
    json body = {{"password", newPassword}};
    request("PUT", "/auth/v1/user", &body, L"", false);
}

std::vector<Client::DailyTotal> Client::dailyTotals(const std::string& dayFrom, const std::string& dayTo)
{
    std::string path = "/rest/v1/activity_daily_totals?select=*&order=day.desc,total_seconds.desc&limit=1000";
    if (!dayFrom.empty() && !dayTo.empty())
        path += "&and=(day.gte." + dayFrom + ",day.lte." + dayTo + ")";
    else if (!dayFrom.empty())
        path += "&day=gte." + dayFrom;
    else if (!dayTo.empty())
        path += "&day=lte." + dayTo;

    const json arr = request("GET", path);
    std::vector<DailyTotal> out;
    for (const auto& r : arr)
        out.push_back({r.value("device_id", ""), r.value("process_name", ""), r.value("day", ""), r.value("total_seconds", int64_t{0})});
    return out;
}

std::vector<Client::RecentLog> Client::recentLogs(int limit)
{
    const std::string path = "/rest/v1/activity_logs?select=device_id,process_name,window_title,duration_seconds,start_time,end_time"
                              "&order=start_time.desc&limit=" + std::to_string(limit);
    const json arr = request("GET", path);
    std::vector<RecentLog> out;
    for (const auto& r : arr)
        out.push_back({r.value("device_id", ""), r.value("process_name", ""), r.value("window_title", ""),
                        r.value("duration_seconds", int64_t{0}), r.value("start_time", int64_t{0}), r.value("end_time", int64_t{0})});
    return out;
}

std::vector<Client::RecentLog> Client::activityLogsForChild(const std::string& childUserId, int64_t sinceUnix, int limit)
{
    std::string path = "/rest/v1/activity_logs?select=device_id,process_name,window_title,duration_seconds,start_time,end_time"
                        "&user_id=eq." + urlEncode(childUserId);
    if (sinceUnix > 0)
        path += "&start_time=gte." + std::to_string(sinceUnix);
    path += "&order=start_time.desc&limit=" + std::to_string(limit);

    const json arr = request("GET", path);
    std::vector<RecentLog> out;
    for (const auto& r : arr)
        out.push_back({r.value("device_id", ""), r.value("process_name", ""), r.value("window_title", ""),
                        r.value("duration_seconds", int64_t{0}), r.value("start_time", int64_t{0}), r.value("end_time", int64_t{0})});
    return out;
}

void Client::ping() { request("GET", "/rest/v1/activity_daily_totals?limit=1"); }

void Client::sendMessage(const Message& msg)
{
    json body = {{"sender_device_id", msg.senderDeviceId}, {"target_device_id", msg.targetDeviceId},
                  {"kind", msg.kind}, {"payload", msg.payload}};
    request("POST", "/rest/v1/device_messages", &body, L"Prefer: return=minimal\r\n", false);
}

namespace {
std::vector<Client::Message> parseMessages(const json& arr)
{
    std::vector<Client::Message> out;
    for (const auto& r : arr)
    {
        Client::Message m;
        m.id = r.value("id", int64_t{0});
        m.senderDeviceId = r.value("sender_device_id", "");
        m.targetDeviceId = r.value("target_device_id", "");
        m.kind = r.value("kind", "");
        m.payload = r.value("payload", "");
        m.createdAt = r.value("created_at", "");
        if (r.contains("read_at") && r["read_at"].is_string())
            m.readAt = r["read_at"].get<std::string>();
        out.push_back(std::move(m));
    }
    return out;
}
} // namespace

std::vector<Client::Message> Client::inbox(const std::string& selfDeviceId, int limit)
{
    const std::string path = "/rest/v1/device_messages?target_device_id=eq." + urlEncode(selfDeviceId) +
                              "&order=created_at.desc&limit=" + std::to_string(limit);
    return parseMessages(request("GET", path));
}

std::vector<Client::Message> Client::thread(const std::string& selfDeviceId, const std::string& otherDeviceId, int limit)
{
    const std::string self = urlEncode(selfDeviceId), other = urlEncode(otherDeviceId);
    const std::string path = "/rest/v1/device_messages?or=(and(sender_device_id.eq." + self +
                              ",target_device_id.eq." + other + "),and(sender_device_id.eq." + other +
                              ",target_device_id.eq." + self + "))&order=created_at.asc&limit=" + std::to_string(limit);
    return parseMessages(request("GET", path));
}

void Client::markRead(int64_t id)
{
    // RFC3339 UTC "now" - PostgREST chấp nhận định dạng ISO8601 chuẩn.
    SYSTEMTIME st; GetSystemTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    json body = {{"read_at", buf}};
    request("PATCH", "/rest/v1/device_messages?id=eq." + std::to_string(id), &body, L"Prefer: return=minimal\r\n", false);
}

void Client::inviteChild(const std::string& childEmail)
{
    json lookupBody = {{"target_email", childEmail}};
    const json idResult = request("POST", "/rest/v1/rpc/find_user_id_by_email", &lookupBody, L"", false);

    std::string childId;
    if (idResult.is_string())
        childId = idResult.get<std::string>();

    if (childId.empty())
        throw ApiError("no JustInTime account found for \"" + childEmail + "\" - they need to sign up/log in with this exact email first");

    json insertBody = {{"child_user_id", childId}};
    try
    {
        request("POST", "/rest/v1/parent_links", &insertBody, L"Prefer: return=minimal\r\n", false);
    }
    catch (const ApiError& e)
    {
        const std::string msg = e.what();
        if (msg.find("409") != std::string::npos)
            throw ApiError("already invited or linked with this account");
        throw;
    }
}

namespace {
std::optional<std::string> optApprovedAt(const json& r)
{
    if (r.contains("approved_at") && r["approved_at"].is_string())
        return r["approved_at"].get<std::string>();
    return std::nullopt;
}
} // namespace

std::vector<Client::ParentLink> Client::listLinksAsParent()
{
    const json arr = request("POST", "/rest/v1/rpc/parent_links_for_parent", nullptr, L"", true);
    std::vector<ParentLink> out;
    for (const auto& r : arr)
        out.push_back({r.value("id", int64_t{0}), r.value("child_user_id", ""), r.value("child_email", ""),
                        r.value("status", ""), r.value("created_at", ""), optApprovedAt(r)});
    return out;
}

std::vector<Client::ParentLink> Client::listLinksAsChild()
{
    const json arr = request("POST", "/rest/v1/rpc/parent_links_for_child", nullptr, L"", true);
    std::vector<ParentLink> out;
    for (const auto& r : arr)
        out.push_back({r.value("id", int64_t{0}), r.value("parent_user_id", ""), r.value("parent_email", ""),
                        r.value("status", ""), r.value("created_at", ""), optApprovedAt(r)});
    return out;
}

void Client::approveLink(int64_t linkId)
{
    json body = {{"status", "approved"}};
    request("PATCH", "/rest/v1/parent_links?id=eq." + std::to_string(linkId), &body, L"Prefer: return=minimal\r\n", false);
}

void Client::revokeLink(int64_t linkId)
{
    json body = {{"status", "revoked"}};
    request("PATCH", "/rest/v1/parent_links?id=eq." + std::to_string(linkId), &body, L"Prefer: return=minimal\r\n", false);
}

std::vector<std::pair<int64_t, std::string>> Client::listPermissions()
{
    const json arr = request("GET", "/rest/v1/parent_links?select=id,permission_level");
    std::vector<std::pair<int64_t, std::string>> out;
    for (const auto& r : arr)
        out.emplace_back(r.value("id", int64_t{0}), r.value("permission_level", "full"));
    return out;
}

void Client::setPermission(int64_t linkId, const std::string& level)
{
    json body = {{"permission_level", level}};
    request("PATCH", "/rest/v1/parent_links?id=eq." + std::to_string(linkId), &body, L"Prefer: return=minimal\r\n", false);
}

std::vector<Client::AppLimit> Client::listLimitsForChild(const std::string& childUserId)
{
    const std::string path = "/rest/v1/app_limits?select=id,process_name,daily_limit_sec,blocked&child_user_id=eq." +
                              urlEncode(childUserId) + "&order=process_name.asc";
    const json arr = request("GET", path);
    std::vector<AppLimit> out;
    for (const auto& r : arr)
    {
        AppLimit l;
        l.id = r.value("id", int64_t{0});
        l.processName = r.value("process_name", "");
        l.blocked = r.value("blocked", false);
        if (r.contains("daily_limit_sec") && r["daily_limit_sec"].is_number())
            l.dailyLimitSec = r["daily_limit_sec"].get<int>();
        out.push_back(std::move(l));
    }
    return out;
}

void Client::setLimit(const std::string& childUserId, const std::string& processName, std::optional<int> dailyLimitSec, bool blocked)
{
    json body = {{"child_user_id", childUserId}, {"process_name", processName}, {"blocked", blocked},
                  {"daily_limit_sec", dailyLimitSec ? json(*dailyLimitSec) : json(nullptr)}};
    request("POST", "/rest/v1/app_limits?on_conflict=child_user_id,process_name", &body,
            L"Prefer: resolution=merge-duplicates,return=minimal\r\n", false);
}

void Client::deleteLimit(int64_t limitId)
{
    request("DELETE", "/rest/v1/app_limits?id=eq." + std::to_string(limitId), nullptr, L"Prefer: return=minimal\r\n", false);
}

void Client::pushHeartbeat(const std::string& deviceId, const std::string& label, double cpu, double ram, double disk)
{
    SYSTEMTIME st; GetSystemTime(&st);
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02dZ", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    json body = {{"device_id", deviceId}, {"hostname", label}, {"cpu_percent", cpu}, {"ram_percent", ram},
                  {"disk_percent", disk}, {"last_seen", buf}};
    request("POST", "/rest/v1/device_heartbeats?on_conflict=user_id,device_id", &body,
            L"Prefer: resolution=merge-duplicates,return=minimal\r\n", false);
}

namespace {
std::vector<Client::DeviceHeartbeat> parseHeartbeats(const json& arr)
{
    std::vector<Client::DeviceHeartbeat> out;
    for (const auto& r : arr)
        out.push_back({r.value("device_id", ""), r.value("hostname", ""), r.value("last_seen", ""),
                        r.value("cpu_percent", 0.0), r.value("ram_percent", 0.0), r.value("disk_percent", 0.0)});
    return out;
}
}

std::vector<Client::DeviceHeartbeat> Client::listHeartbeats()
{
    return parseHeartbeats(request("GET", "/rest/v1/device_heartbeats?select=*&order=last_seen.desc"));
}

std::vector<Client::DeviceHeartbeat> Client::listHeartbeatsForChild(const std::string& childUserId)
{
    const std::string path = "/rest/v1/device_heartbeats?select=*&user_id=eq." + urlEncode(childUserId) + "&order=last_seen.desc";
    return parseHeartbeats(request("GET", path));
}

void Client::deleteHeartbeat(const std::string& deviceId)
{
    request("DELETE", "/rest/v1/device_heartbeats?device_id=eq." + urlEncode(deviceId), nullptr, L"Prefer: return=minimal\r\n", false);
}

std::vector<std::string> distinctDevices(const std::vector<Client::DailyTotal>& totals)
{
    std::vector<std::string> out;
    for (const auto& t : totals)
        if (std::find(out.begin(), out.end(), t.deviceId) == out.end())
            out.push_back(t.deviceId);
    std::sort(out.begin(), out.end());
    return out;
}

} // namespace jit::dash::cloud
