//
// applimits.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface extern "C" trong
// applimits.h). Logic giữ nguyên 100% - chỉ đổi cách dựng
// path/body sang std::string thay vì swprintf/snprintf vào buffer
// cấp phát tay ở từng hàm.
//

#include "applimits.h"
#include "restclient.h"
#include "jsonutil.h"
#include "strutil.h"
#include "paths.h"
#include "auth.h"
#include "log.h"

#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

namespace {

using jit::utf8ToWide;
using jit::jsonEscape;

// ---- Bộ nhớ đệm giới hạn của CHÍNH MÁY NÀY (xem applimits.h) ----

std::mutex g_cacheMutex;
std::vector<AppLimit> g_cache;
std::string g_cacheUserId;      // tài khoản mà bộ nhớ đệm thuộc về
bool g_cacheLoaded = false;

std::filesystem::path cacheFile()
{
    return jit::configFile(L"limits.cache");
}

/*
 * Định dạng file (văn bản, 1 mục/dòng):
 *   user=<user_id>
 *   <process_name>\t<daily_limit_sec>\t<blocked>
 * Gắn với user_id để nếu máy đổi sang tài khoản khác thì không áp nhầm
 * giới hạn của tài khoản cũ.
 */
void saveCacheToDisk()
{
    std::ostringstream out;
    out << "user=" << g_cacheUserId << "\n";

    for (const AppLimit& l : g_cache)
        out << l.process_name << '\t' << l.daily_limit_sec << '\t' << l.blocked << '\n';

    jit::writeFileAtomic(cacheFile(), out.str());
}

void loadCacheFromDiskLocked()
{
    g_cacheLoaded = true;
    g_cache.clear();
    g_cacheUserId.clear();

    std::ifstream f(cacheFile(), std::ios::in | std::ios::binary);
    if (!f)
        return;

    std::string line;
    while (std::getline(f, line))
    {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.rfind("user=", 0) == 0)
        {
            g_cacheUserId = line.substr(5);
            continue;
        }

        const size_t t1 = line.find('\t');
        const size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
        if (t2 == std::string::npos || t1 == 0 || g_cache.size() >= MAX_LIMITS)
            continue;

        AppLimit l;
        memset(&l, 0, sizeof(l));
        snprintf(l.process_name, sizeof(l.process_name), "%s", line.substr(0, t1).c_str());
        l.daily_limit_sec = std::atoi(line.substr(t1 + 1, t2 - t1 - 1).c_str());
        l.blocked = std::atoi(line.substr(t2 + 1).c_str()) != 0 ? 1 : 0;
        g_cache.push_back(l);
    }
}

int parseLimitsResponse(
    const char* response,
    AppLimit* out, int max_out)
{
    int count = 0;
    const char* cursor = response;
    char obj[2048];

    while (count < max_out && json_array_next(&cursor, obj, sizeof(obj)))
    {
        AppLimit* limit = &out[count];
        memset(limit, 0, sizeof(*limit));

        long id_val = 0;
        json_extract_long(obj, "id", &id_val, NULL);
        limit->id = id_val;

        json_extract_string(obj, "process_name", limit->process_name, sizeof(limit->process_name));

        long daily_val = 0;
        int is_null = 0;

        if (json_extract_long(obj, "daily_limit_sec", &daily_val, &is_null) && !is_null)
            limit->daily_limit_sec = static_cast<int>(daily_val);
        else
            limit->daily_limit_sec = -1;

        int blocked_val = 0;
        json_extract_bool(obj, "blocked", &blocked_val);
        limit->blocked = blocked_val;

        count++;
    }

    return count;
}

} // namespace

namespace {

/* Trả về số giới hạn lấy được (có thể = 0 nếu server nói "không có giới
 * hạn nào"), hoặc -1 nếu KHÔNG hỏi được server (mất mạng/HTTP lỗi) - để
 * phân biệt "chưa có giới hạn" với "không biết", điều mà bản cũ không làm
 * được (cả hai đều trả 0). */
int fetchLimits(const char* child_user_id, AppLimit* out, int max_out)
{
    if (!auth_is_logged_in() || !child_user_id || child_user_id[0] == '\0')
        return -1;

    const std::wstring childIdW = utf8ToWide(child_user_id);
    const std::wstring path =
        L"/rest/v1/app_limits?select=*&child_user_id=eq." + childIdW + L"&order=process_name.asc";

    char response[16384] = {0};
    DWORD status = 0;

    if (!restclient_call("GET", path.c_str(), NULL, NULL, response, sizeof(response), &status))
        return -1;

    if (status < 200 || status >= 300)
        return -1;

    return parseLimitsResponse(response, out, max_out);
}

} // namespace

int applimits_list_for_child(
    const char* child_user_id,
    AppLimit* out, int max_out)
{
    const int n = fetchLimits(child_user_id, out, max_out);
    return n > 0 ? n : 0;
}

int applimits_set(
    const char* child_user_id,
    const char* process_name,
    int daily_limit_sec,
    int blocked,
    char* err_out, int err_out_size)
{
    if (!auth_is_logged_in())
    {
        snprintf(err_out, err_out_size, "Ban can dang nhap truoc.");
        return 0;
    }

    if (!child_user_id || !process_name || process_name[0] == '\0')
    {
        snprintf(err_out, err_out_size, "Thieu thong tin process/child.");
        return 0;
    }

    const std::string procEsc = jsonEscape(process_name);
    const char* blockedStr = blocked ? "true" : "false";

    std::string body = "{\"child_user_id\":\"" + std::string(child_user_id) +
                        "\",\"process_name\":\"" + procEsc + "\",\"daily_limit_sec\":";

    if (daily_limit_sec >= 0)
        body += std::to_string(daily_limit_sec);
    else
        body += "null";

    body += ",\"blocked\":" + std::string(blockedStr) + "}";

    char response[2048] = {0};
    DWORD status = 0;

    const int ok = restclient_call(
        "POST",
        L"/rest/v1/app_limits?on_conflict=child_user_id,process_name",
        body.c_str(),
        L"Prefer: resolution=merge-duplicates,return=minimal\r\n",
        response, sizeof(response), &status
    );

    if (!ok)
    {
        snprintf(err_out, err_out_size, "Khong the ket noi toi Supabase.");
        return 0;
    }

    if (status >= 200 && status < 300)
        return 1;

    snprintf(err_out, err_out_size, "Loi tu server (HTTP %lu): %s", status, response);
    return 0;
}

int applimits_delete(
    long limit_id,
    char* err_out, int err_out_size)
{
    if (!auth_is_logged_in())
    {
        snprintf(err_out, err_out_size, "Ban can dang nhap truoc.");
        return 0;
    }

    const std::wstring path = L"/rest/v1/app_limits?id=eq." + std::to_wstring(limit_id);

    char response[2048] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "DELETE", path.c_str(), NULL,
            L"Prefer: return=minimal\r\n",
            response, sizeof(response), &status
        )
    )
    {
        snprintf(err_out, err_out_size, "Khong the ket noi toi Supabase.");
        return 0;
    }

    if (status >= 200 && status < 300)
        return 1;

    snprintf(err_out, err_out_size, "Loi tu server (HTTP %lu): %s", status, response);
    return 0;
}

int applimits_get_my_limits(AppLimit* out, int max_out)
{
    if (!out || max_out <= 0 || !auth_is_logged_in())
        return 0;

    AuthSession session;
    auth_get_session(&session);

    std::lock_guard<std::mutex> lock(g_cacheMutex);

    if (!g_cacheLoaded)
        loadCacheFromDiskLocked();

    // Bộ nhớ đệm của tài khoản khác (hoặc chưa từng làm mới) -> không áp dụng.
    if (g_cacheUserId != session.user_id)
        return 0;

    const int n = static_cast<int>(g_cache.size()) < max_out ? static_cast<int>(g_cache.size()) : max_out;
    for (int i = 0; i < n; i++)
        out[i] = g_cache[static_cast<size_t>(i)];

    return n;
}

int applimits_refresh_my_limits(void)
{
    if (!auth_is_logged_in())
        return 0;

    AuthSession session;
    auth_get_session(&session);

    std::vector<AppLimit> fetched(MAX_LIMITS);
    const int n = fetchLimits(session.user_id, fetched.data(), MAX_LIMITS);

    if (n < 0)
        return 0; // không hỏi được server -> giữ nguyên bộ nhớ đệm cũ

    fetched.resize(static_cast<size_t>(n));

    std::lock_guard<std::mutex> lock(g_cacheMutex);

    const bool changed = !g_cacheLoaded || g_cacheUserId != session.user_id ||
        g_cache.size() != fetched.size() ||
        (n > 0 && memcmp(g_cache.data(), fetched.data(), sizeof(AppLimit) * fetched.size()) != 0);

    g_cacheLoaded = true;
    g_cacheUserId = session.user_id;
    g_cache = std::move(fetched);

    if (changed)
    {
        saveCacheToDisk();
        JIT_LOG(L"[LIMITS] Da cap nhat %d gioi han app tu server\n", n);
    }

    return 1;
}
