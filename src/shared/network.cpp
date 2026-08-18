//
// network.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface
// extern "C" trong network.h), hành vi 100% giống bản C gốc trước khi chuyển đổi (gửi
// qua Edge Function "sync-activity", tự refresh token 1 lần nếu gặp
// 401). Đổi cách viết:
//   - RAII cho HINTERNET (dùng lại ý tưởng từ
//     src/shared/restclient.cpp) - bản C cũ có 3 tầng
//     if lồng nhau (hSession/hConnect/hRequest) để đóng handle đúng
//     thứ tự, dễ quên nếu sửa thêm nhánh mới.
//   - std::string cho phần build JSON body.
//

#include "network.h"
#include "config.h"
#include "jsonutil.h"
#include "settings.h"
#include "auth.h"
#include "error_codes.h"

#include <windows.h>
#include <winhttp.h>

#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace {

class WinHttpHandle
{
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET h) : m_handle(h) {}
    ~WinHttpHandle() { if (m_handle) WinHttpCloseHandle(m_handle); }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    WinHttpHandle(WinHttpHandle&& o) noexcept : m_handle(o.m_handle) { o.m_handle = nullptr; }

    HINTERNET get() const { return m_handle; }
    explicit operator bool() const { return m_handle != nullptr; }

private:
    HINTERNET m_handle = nullptr;
};

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

std::wstring extractHost(const std::string& url)
{
    size_t pos = url.find("://");
    std::string hostPart = (pos != std::string::npos) ? url.substr(pos + 3) : url;
    return utf8ToWide(hostPart.c_str());
}

std::string jsonEscape(const std::string& s)
{
    std::string out(s.size() * 6 + 16, '\0');
    json_escape(s.c_str(), out.data(), out.size());
    out.resize(strlen(out.c_str()));
    return out;
}

/*
 * Chuyển 1 SyncRecord thành JSON body để POST lên Edge Function.
 * Không gửi "id" cục bộ, vì id này chỉ có ý nghĩa trong SQLite của
 * từng máy, không phải khoá chung.
 */
int buildJsonBody(const SyncRecord* rec, char* out, int out_size)
{
    char processUtf8[1024] = {0};
    char titleUtf8[4096] = {0};

    WideCharToMultiByte(CP_UTF8, 0, rec->process_name, -1, processUtf8, sizeof(processUtf8), NULL, NULL);
    WideCharToMultiByte(CP_UTF8, 0, rec->window_title, -1, titleUtf8, sizeof(titleUtf8), NULL, NULL);

    const std::string deviceEsc = jsonEscape(rec->device_id);
    const std::string processEsc = jsonEscape(processUtf8);
    const std::string titleEsc = jsonEscape(titleUtf8);

    return snprintf(
        out, out_size,
        "{"
        "\"device_id\":\"%s\","
        "\"process_name\":\"%s\","
        "\"window_title\":\"%s\","
        "\"duration_seconds\":%ld,"
        "\"start_time\":%lld,"
        "\"end_time\":%lld"
        "}",
        deviceEsc.c_str(), processEsc.c_str(), titleEsc.c_str(),
        rec->duration_seconds, rec->start_time, rec->end_time
    );
}

/*
 * Thực hiện đúng 1 lần gọi HTTP POST tới Edge Function sync-activity
 * với access_token truyền vào - tách riêng để network_send_record()
 * có thể gọi lại lần 2 sau khi refresh token.
 */
int trySendRecord(
    const char* body, int bodyLen,
    const char* accessTokenStr,
    DWORD* statusOut,
    char* respOut, int respOutSize)
{
    char baseUrl[MAX_URL_LEN] = {0};
    char apikeyStr[MAX_KEY_LEN] = {0};

    settings_get_supabase_config(baseUrl, sizeof(baseUrl), apikeyStr, sizeof(apikeyStr));

    const std::wstring host = extractHost(baseUrl);
    const std::wstring apikey = utf8ToWide(apikeyStr);
    const std::wstring accessToken = utf8ToWide(accessTokenStr);

    WinHttpHandle hSession(WinHttpOpen(
        L"JustInTime-Agent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0
    ));
    if (!hSession)
    {
        wprintf(L"[SYNC] WinHttpOpen that bai (%lu)\n", GetLastError());
        return 0;
    }

    WinHttpHandle hConnect(WinHttpConnect(hSession.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!hConnect)
    {
        wprintf(L"[SYNC][%hs] WinHttpConnect that bai (%lu)\n", ERR_SYNC_CONNECT_FAIL, GetLastError());
        return 0;
    }

    WinHttpHandle hRequest(WinHttpOpenRequest(
        hConnect.get(), L"POST", L"/functions/v1/sync-activity",
        NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE
    ));
    if (!hRequest)
    {
        wprintf(L"[SYNC][%hs] WinHttpOpenRequest that bai (%lu)\n", ERR_SYNC_CONNECT_FAIL, GetLastError());
        return 0;
    }

    wchar_t headers[4096];
    swprintf(
        headers, 4096,
        L"Content-Type: application/json\r\n"
        L"apikey: %ls\r\n"
        L"Authorization: Bearer %ls\r\n",
        apikey.c_str(), accessToken.c_str()
    );

    BOOL sent = WinHttpSendRequest(
        hRequest.get(), headers, static_cast<DWORD>(-1),
        const_cast<LPVOID>(static_cast<const void*>(body)),
        static_cast<DWORD>(bodyLen), static_cast<DWORD>(bodyLen), 0
    );

    if (!sent || !WinHttpReceiveResponse(hRequest.get(), NULL))
    {
        wprintf(L"[SYNC][%hs] Gui request that bai (%lu)\n", ERR_SYNC_CONNECT_FAIL, GetLastError());
        return 0;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(
        hRequest.get(),
        WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX
    );

    DWORD totalRead = 0;
    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(hRequest.get(), &available) || available == 0)
            break;

        DWORD remaining = static_cast<DWORD>(respOutSize) - 1 - totalRead;
        if (available > remaining)
            available = remaining;
        if (available == 0)
            break;

        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest.get(), respOut + totalRead, available, &bytesRead) || bytesRead == 0)
            break;

        totalRead += bytesRead;
    }

    respOut[totalRead] = '\0';
    *statusOut = status;
    return 1;
}

} // namespace

int network_send_record(const SyncRecord* rec)
{
    if (!rec)
        return 0;

    /*
     * Bắt buộc phải đăng nhập mới sync lên cloud được. Dữ liệu vẫn
     * được lưu đầy đủ ở local (SQLite + backup JSON) dù chưa đăng
     * nhập, chỉ là chưa lên được cloud.
     */
    if (!auth_is_logged_in())
        return 0;

    AuthSession session;
    auth_get_session(&session);

    char body[16384] = {0};
    int bodyLen = buildJsonBody(rec, body, sizeof(body));

    if (bodyLen <= 0 || bodyLen >= static_cast<int>(sizeof(body)))
    {
        wprintf(L"[SYNC][%hs] Khong the tao JSON body (qua dai hoac loi)\n", ERR_SYNC_PAYLOAD_TOO_BIG);
        return 0;
    }

    DWORD status = 0;
    char respBody[4096] = {0};

    if (!trySendRecord(body, bodyLen, session.access_token, &status, respBody, sizeof(respBody)))
        return 0;

    if (status >= 200 && status < 300)
        return 1;

    if (status == 401)
    {
        wprintf(
            L"[SYNC][%hs] Supabase tra ve loi HTTP 401 (access_token co the da het han): %hs\n"
            L"[SYNC] Dang thu refresh token...\n",
            ERR_SYNC_SERVER_ERROR, respBody
        );

        if (auth_refresh_session())
        {
            AuthSession refreshed;
            auth_get_session(&refreshed);

            DWORD retryStatus = 0;
            char retryResp[4096] = {0};

            if (
                trySendRecord(body, bodyLen, refreshed.access_token, &retryStatus, retryResp, sizeof(retryResp))
                && retryStatus >= 200 && retryStatus < 300
            )
            {
                wprintf(L"[SYNC] Gui lai sau khi refresh token thanh cong\n");
                return 1;
            }

            wprintf(
                L"[SYNC][%hs] Van that bai sau khi refresh token (HTTP %lu): %hs\n",
                ERR_SYNC_SERVER_ERROR, retryStatus, retryResp
            );
        }
        else
        {
            wprintf(L"[SYNC][%hs] Refresh token that bai - can dang nhap lai qua tray\n", ERR_AUTH_REFRESH_FAIL);
        }

        return 0;
    }

    wprintf(L"[SYNC][%hs] Supabase tra ve loi HTTP %lu: %hs\n", ERR_SYNC_SERVER_ERROR, status, respBody);
    return 0;
}
