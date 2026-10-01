//
// restclient.cpp
//
// Viết lại trên nền jit::httpsRequest (httpclient.h). Interface extern "C"
// trong restclient.h giữ nguyên - mọi nơi gọi (parentlink, applimits,
// machines) không phải đổi gì. Khác biệt về hành vi:
//   - Tái sử dụng kết nối TLS giữa các lần gọi (mỗi thread 1 kết nối).
//   - Có timeout.
//   - Tự refresh access_token + thử lại 1 lần khi gặp HTTP 401. Trước đây
//     chỉ đường gửi activity (network.cpp) mới refresh; heartbeat, danh sách
//     giới hạn app, parent-link... cứ 401 là im lặng thất bại cho tới khi
//     có ai đó gọi sync.
//   - Header không còn bị giới hạn ở buffer 4096 ký tự.
//

#include "restclient.h"
#include "httpclient.h"
#include "strutil.h"
#include "settings.h"
#include "auth.h"
#include "log.h"

#include <cstring>
#include <mutex>
#include <string>

extern "C" int restclient_refresh_if_stale(const char* used_token)
{
    static std::mutex refreshMutex;
    std::lock_guard<std::mutex> lock(refreshMutex);

    AuthSession current;
    auth_get_session(&current);

    if (!current.logged_in)
        return 0;

    if (used_token && used_token[0] != '\0' &&
        strcmp(current.access_token, used_token) != 0)
    {
        return 1; // luồng khác đã refresh trong lúc ta chờ khoá
    }

    return auth_refresh_session();
}

extern "C" int restclient_call(
    const char* method,
    const wchar_t* path,
    const char* body,
    const wchar_t* extra_headers,
    char* response_out, int response_out_size,
    DWORD* status_out)
{
    if (!method || !path || !response_out || !status_out || response_out_size <= 0)
        return 0;

    char base_url[MAX_URL_LEN] = {0};
    char apikey_str[MAX_KEY_LEN] = {0};

    settings_get_supabase_config(base_url, sizeof(base_url), apikey_str, sizeof(apikey_str));

    const std::wstring host = jit::utf8ToWide(jit::hostFromUrl(base_url));
    const std::wstring apikey = jit::utf8ToWide(apikey_str);
    const std::wstring methodW = jit::utf8ToWide(method);
    const std::string bodyStr = body ? body : "";
    const bool idempotent = (strcmp(method, "GET") == 0);

    jit::HttpResult result;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        AuthSession session;
        auth_get_session(&session);

        std::wstring headers = L"Content-Type: application/json\r\napikey: ";
        headers += apikey;
        headers += L"\r\nAuthorization: Bearer ";
        headers += jit::utf8ToWide(session.access_token);
        headers += L"\r\n";
        if (extra_headers)
            headers += extra_headers;

        if (!jit::httpsRequest(host, methodW.c_str(), path, headers, bodyStr, result, idempotent))
        {
            JIT_LOG(L"[REST] Request that bai (khong ket noi duoc)\n");
            return 0;
        }

        if (result.status == 401 && attempt == 0 && session.logged_in &&
            restclient_refresh_if_stale(session.access_token))
        {
            continue; // thử lại 1 lần với token mới
        }

        break;
    }

    const size_t copyLen = result.body.size() < static_cast<size_t>(response_out_size - 1)
        ? result.body.size()
        : static_cast<size_t>(response_out_size - 1);

    memcpy(response_out, result.body.data(), copyLen);
    response_out[copyLen] = '\0';
    *status_out = result.status;
    return 1;
}
