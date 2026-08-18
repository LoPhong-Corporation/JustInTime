//
// restclient.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên 100% interface extern "C" trong
// restclient.h - mọi file .c/.cpp gọi vào đây không cần đổi gì cả).
//
// Thay đổi thật sự nằm ở CÁCH VIẾT bên trong:
//   - RAII cho HINTERNET (lớp WinHttpHandle) thay vì tự gọi
//     WinHttpCloseHandle() thủ công ở từng nhánh if/else - bản C cũ
//     tuy đúng nhưng rất dễ rò rỉ handle nếu sau này ai đó thêm 1
//     "return sớm" mới mà quên đóng handle ở nhánh đó. Với RAII,
//     handle LUÔN được đóng khi ra khỏi scope, bất kể ra bằng đường
//     nào (kể cả exception, dù code này không ném exception).
//   - std::wstring/std::string thay vì buffer wchar_t/char cấp phát
//     tay với size cố định phải đoán trước (vd wchar_t apikey[2048]
//     trong bản cũ) - giảm hẳn 1 lớp lỗi tràn bộ đệm tiềm ẩn.
//   - Luồng điều khiển phẳng (early return) thay vì lồng if/else 3-4
//     cấp như bản C cũ.
//

#include "restclient.h"
#include "config.h"
#include "settings.h"
#include "auth.h"

#include <winhttp.h>

#include <cstdio>
#include <cstring>
#include <string>

#pragma comment(lib, "winhttp.lib")

namespace {

// RAII cho HINTERNET - tự động WinHttpCloseHandle() khi ra khỏi
// scope. Không cho copy (mỗi handle chỉ có 1 chủ sở hữu), cho phép
// move.
class WinHttpHandle
{
public:
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET h) : m_handle(h) {}

    ~WinHttpHandle()
    {
        if (m_handle)
            WinHttpCloseHandle(m_handle);
    }

    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    WinHttpHandle(WinHttpHandle&& other) noexcept : m_handle(other.m_handle)
    {
        other.m_handle = nullptr;
    }

    WinHttpHandle& operator=(WinHttpHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (m_handle)
                WinHttpCloseHandle(m_handle);
            m_handle = other.m_handle;
            other.m_handle = nullptr;
        }
        return *this;
    }

    HINTERNET get() const { return m_handle; }
    explicit operator bool() const { return m_handle != nullptr; }

private:
    HINTERNET m_handle = nullptr;
};

// Tách phần host ra khỏi SUPABASE_URL dạng
// "https://xxxx.supabase.co" -> "xxxx.supabase.co"
std::wstring extract_host(const std::string& url)
{
    size_t pos = url.find("://");
    std::string hostPart = (pos != std::string::npos) ? url.substr(pos + 3) : url;

    int wlen = MultiByteToWideChar(CP_UTF8, 0, hostPart.c_str(), -1, nullptr, 0);
    std::wstring result(wlen > 0 ? wlen - 1 : 0, L'\0');
    if (wlen > 0)
        MultiByteToWideChar(CP_UTF8, 0, hostPart.c_str(), -1, result.data(), wlen);
    return result;
}

std::wstring utf8_to_wstring(const std::string& s)
{
    if (s.empty())
        return L"";
    int wlen = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring result(wlen > 0 ? wlen - 1 : 0, L'\0');
    if (wlen > 0)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, result.data(), wlen);
    return result;
}

} // namespace

int restclient_call(
    const char* method,
    const wchar_t* path,
    const char* body,
    const wchar_t* extra_headers,
    char* response_out, int response_out_size,
    DWORD* status_out)
{
    if (!method || !path || !response_out || !status_out || response_out_size <= 0)
        return 0;

    AuthSession session;
    auth_get_session(&session);

    char base_url[MAX_URL_LEN] = {0};
    char apikey_str[MAX_KEY_LEN] = {0};

    settings_get_supabase_config(
        base_url, sizeof(base_url),
        apikey_str, sizeof(apikey_str)
    );

    const std::wstring host = extract_host(base_url);
    const std::wstring apikey = utf8_to_wstring(apikey_str);
    const std::wstring access_token = utf8_to_wstring(session.access_token);
    const std::wstring method_w = utf8_to_wstring(method);

    WinHttpHandle hSession(WinHttpOpen(
        L"JustInTime-Agent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    ));

    if (!hSession)
    {
        wprintf(L"[REST] WinHttpOpen that bai (%lu)\n", GetLastError());
        return 0;
    }

    WinHttpHandle hConnect(WinHttpConnect(
        hSession.get(),
        host.c_str(),
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    ));

    if (!hConnect)
    {
        wprintf(L"[REST] WinHttpConnect that bai (%lu)\n", GetLastError());
        return 0;
    }

    WinHttpHandle hRequest(WinHttpOpenRequest(
        hConnect.get(),
        method_w.c_str(),
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    ));

    if (!hRequest)
    {
        wprintf(L"[REST] WinHttpOpenRequest that bai (%lu)\n", GetLastError());
        return 0;
    }

    wchar_t headers[4096];
    swprintf(
        headers,
        4096,
        L"Content-Type: application/json\r\n"
        L"apikey: %ls\r\n"
        L"Authorization: Bearer %ls\r\n"
        L"%ls",
        apikey.c_str(),
        access_token.c_str(),
        extra_headers ? extra_headers : L""
    );

    const int body_len = body ? static_cast<int>(strlen(body)) : 0;

    BOOL sent = WinHttpSendRequest(
        hRequest.get(),
        headers,
        static_cast<DWORD>(-1),
        const_cast<LPVOID>(static_cast<const void*>(body)),
        static_cast<DWORD>(body_len),
        static_cast<DWORD>(body_len),
        0
    );

    if (!sent || !WinHttpReceiveResponse(hRequest.get(), NULL))
    {
        wprintf(L"[REST] Gui request that bai (%lu)\n", GetLastError());
        return 0;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);

    WinHttpQueryHeaders(
        hRequest.get(),
        WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &status,
        &status_size,
        WINHTTP_NO_HEADER_INDEX
    );

    DWORD total_read = 0;

    for (;;)
    {
        DWORD available = 0;

        if (!WinHttpQueryDataAvailable(hRequest.get(), &available) || available == 0)
            break;

        DWORD remaining = static_cast<DWORD>(response_out_size) - 1 - total_read;
        if (available > remaining)
            available = remaining;

        if (available == 0)
            break;

        DWORD bytes_read = 0;

        if (
            !WinHttpReadData(hRequest.get(), response_out + total_read, available, &bytes_read)
            || bytes_read == 0
        )
            break;

        total_read += bytes_read;
    }

    response_out[total_read] = '\0';
    *status_out = status;

    // hRequest/hConnect/hSession tự đóng khi ra khỏi scope ở đây
    // (destructor của WinHttpHandle) - không cần dọn tay.
    return 1;
}
