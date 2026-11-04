//
// network.cpp
//
// Gửi activity lên Edge Function "sync-activity" (tự refresh token 1 lần
// nếu gặp 401). Viết lại trên nền jit::httpsRequest (httpclient.h):
//   - Gửi THEO LÔ (mảng JSON) trong 1 request thay vì 1 request/record.
//     Trước đây sync 100 record = 100 lần WinHttpOpen + TLS handshake +
//     100 lần server gọi auth.getUser() + 100 lần upsert.
//   - Tái sử dụng kết nối, có timeout (xem httpclient.h).
//   - Không còn buffer body 16KB cố định.
//

#include "network.h"
#include "httpclient.h"
#include "restclient.h"
#include "strutil.h"
#include "settings.h"
#include "auth.h"
#include "error_codes.h"
#include "log.h"

#include <cwchar>
#include <string>

namespace {

void appendRecordJson(std::string& out, const SyncRecord& r)
{
    const std::wstring_view process(r.process_name, wcsnlen(r.process_name, 512));
    const std::wstring_view title(r.window_title, wcsnlen(r.window_title, 2048));

    // Không gửi "id" cục bộ: id chỉ có nghĩa trong SQLite của từng máy.
    out += "{\"device_id\":\"";
    out += jit::jsonEscape(r.device_id);
    out += "\",\"process_name\":\"";
    out += jit::jsonEscape(jit::wideToUtf8(process));
    out += "\",\"window_title\":\"";
    out += jit::jsonEscape(jit::wideToUtf8(title));
    out += "\",\"duration_seconds\":";
    out += std::to_string(r.duration_seconds);
    out += ",\"start_time\":";
    out += std::to_string(r.start_time);
    out += ",\"end_time\":";
    out += std::to_string(r.end_time);
    out += "}";
}

/*
 * 1 record -> object đơn (tương thích với phiên bản Edge Function cũ chưa
 * hỗ trợ mảng); nhiều record -> mảng.
 */
std::string buildBody(const SyncRecord* records, int count)
{
    std::string body;
    body.reserve(static_cast<size_t>(count) * 512 + 16);

    if (count == 1)
    {
        appendRecordJson(body, records[0]);
        return body;
    }

    body += '[';
    for (int i = 0; i < count; i++)
    {
        if (i > 0)
            body += ',';
        appendRecordJson(body, records[i]);
    }
    body += ']';
    return body;
}

// Lỗi phía hạ tầng/quá tải - tạm thời, không phải do dữ liệu của ta.
bool isTransientStatus(DWORD status)
{
    return status == 408 || status == 429 || status == 502 || status == 503 || status == 504;
}

} // namespace

extern "C" int network_send_batch(const SyncRecord* records, int count, int* transport_error)
{
    if (transport_error)
        *transport_error = 0;

    if (!records || count <= 0)
        return 0;

    /*
     * Bắt buộc phải đăng nhập mới sync lên cloud được. Dữ liệu vẫn được
     * lưu đầy đủ ở local (SQLite + backup JSON) dù chưa đăng nhập.
     */
    if (!auth_is_logged_in())
    {
        if (transport_error)
            *transport_error = 1; // "chưa gửi được lúc này", không phải record lỗi
        return 0;
    }

    char baseUrl[MAX_URL_LEN] = {0};
    char apikeyStr[MAX_KEY_LEN] = {0};
    settings_get_supabase_config(baseUrl, sizeof(baseUrl), apikeyStr, sizeof(apikeyStr));

    const std::wstring host = jit::utf8ToWide(jit::hostFromUrl(baseUrl));
    const std::wstring apikey = jit::utf8ToWide(apikeyStr);
    const std::string body = buildBody(records, count);

    for (int attempt = 0; attempt < 2; attempt++)
    {
        AuthSession session;
        auth_get_session(&session);

        std::wstring headers = L"Content-Type: application/json\r\napikey: ";
        headers += apikey;
        headers += L"\r\nAuthorization: Bearer ";
        headers += jit::utf8ToWide(session.access_token);
        headers += L"\r\n";

        jit::HttpResult result;

        // Upsert theo (user_id, device_id, start_time) => gửi lại an toàn.
        if (!jit::httpsRequest(host, L"POST", L"/functions/v1/sync-activity", headers, body, result, true))
        {
            JIT_LOG(L"[SYNC][%hs] Khong ket noi duoc toi server\n", ERR_SYNC_CONNECT_FAIL);
            if (transport_error)
                *transport_error = 1;
            return 0;
        }

        if (result.status >= 200 && result.status < 300)
            return 1;

        if (result.status == 401 && attempt == 0)
        {
            JIT_LOG(L"[SYNC] HTTP 401 (access_token het han?) - dang thu refresh token...\n");

            if (restclient_refresh_if_stale(session.access_token))
                continue;

            JIT_LOG(L"[SYNC][%hs] Refresh token that bai - can dang nhap lai qua tray\n", ERR_AUTH_REFRESH_FAIL);
            if (transport_error)
                *transport_error = 1; // chưa có token hợp lệ: đừng phạt record
            return 0;
        }

        JIT_LOG(L"[SYNC][%hs] Server tra ve HTTP %lu (%d record)\n",
                ERR_SYNC_SERVER_ERROR, result.status, count);

        if (transport_error)
            *transport_error = isTransientStatus(result.status) ? 1 : 0;
        return 0;
    }

    return 0;
}

extern "C" int network_send_record(const SyncRecord* rec)
{
    return network_send_batch(rec, 1, nullptr);
}
