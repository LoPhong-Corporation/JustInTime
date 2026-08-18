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
#include "auth.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::wstring utf8ToWide(const char* s)
{
    if (!s || !*s)
        return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring result(wlen > 0 ? wlen - 1 : 0, L'\0');
    if (wlen > 0)
        MultiByteToWideChar(CP_UTF8, 0, s, -1, result.data(), wlen);
    return result;
}

std::string jsonEscape(const std::string& s)
{
    std::string out(s.size() * 6 + 16, '\0');
    json_escape(s.c_str(), out.data(), out.size());
    out.resize(strlen(out.c_str()));
    return out;
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

int applimits_list_for_child(
    const char* child_user_id,
    AppLimit* out, int max_out)
{
    if (!auth_is_logged_in() || !child_user_id || child_user_id[0] == '\0')
        return 0;

    const std::wstring childIdW = utf8ToWide(child_user_id);
    const std::wstring path =
        L"/rest/v1/app_limits?select=*&child_user_id=eq." + childIdW + L"&order=process_name.asc";

    char response[16384] = {0};
    DWORD status = 0;

    if (!restclient_call("GET", path.c_str(), NULL, NULL, response, sizeof(response), &status))
        return 0;

    if (status < 200 || status >= 300)
        return 0;

    return parseLimitsResponse(response, out, max_out);
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
    if (!auth_is_logged_in())
        return 0;

    AuthSession session;
    auth_get_session(&session);

    return applimits_list_for_child(session.user_id, out, max_out);
}
