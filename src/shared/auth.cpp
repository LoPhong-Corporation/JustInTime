//
// auth.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface
// extern "C" trong auth.h), hành vi 100% giống bản C gốc trước khi chuyển đổi (đăng nhập/
// đăng ký/refresh qua Supabase Auth, lưu session mã hoá DPAPI). Đổi
// cách viết:
//   - RAII cho HINTERNET (lớp WinHttpHandle, giống restclient.cpp).
//   - std::mutex (magic static) thay CRITICAL_SECTION tự quản.
//   - std::string cho phần build JSON body và đọc/ghi session.dat.
//   - RAII nhỏ cho DATA_BLOB của DPAPI (tự LocalFree khi ra scope) -
//     bản C cũ có 1 nhánh (mở file thất bại sau khi CryptProtectData
//     thành công) phải nhớ tự LocalFree(out_blob.pbData) thủ công;
//     RAII loại bỏ khả năng quên việc này ở bất kỳ nhánh nào trong
//     tương lai.
//

#include "auth.h"
#include "settings.h"
#include "jsonutil.h"
#include "error_codes.h"

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include <fstream>
#include <vector>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

namespace {

std::mutex& sessionMutex()
{
    static std::mutex m;
    return m;
}

AuthSession g_session;

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

// RAII cho con trỏ do CryptProtectData/CryptUnprotectData cấp phát -
// phải giải phóng bằng LocalFree(), khác với "new"/"malloc" thường.
class LocalAllocGuard
{
public:
    explicit LocalAllocGuard(void* p) : m_ptr(p) {}
    ~LocalAllocGuard() { if (m_ptr) LocalFree(m_ptr); }
    LocalAllocGuard(const LocalAllocGuard&) = delete;
    LocalAllocGuard& operator=(const LocalAllocGuard&) = delete;

private:
    void* m_ptr;
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
    return utf8ToWide((pos != std::string::npos ? url.substr(pos + 3) : url).c_str());
}

std::string jsonEscape(const std::string& s)
{
    std::string out(s.size() * 6 + 16, '\0');
    json_escape(s.c_str(), out.data(), out.size());
    out.resize(strlen(out.c_str()));
    return out;
}

/*
 * Gửi POST request tới 1 endpoint của Supabase Auth (GoTrue), trả
 * về status code + toàn bộ response body.
 */
bool doAuthPost(
    const std::string& baseUrl,
    const std::string& apikeyStr,
    const wchar_t* path,
    const std::string& jsonBody,
    char* responseOut, int responseOutSize,
    DWORD* statusOut)
{
    const std::wstring host = extractHost(baseUrl);
    const std::wstring apikey = utf8ToWide(apikeyStr.c_str());

    WinHttpHandle hSession(WinHttpOpen(
        L"JustInTime-Agent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0
    ));
    if (!hSession)
        return false;

    WinHttpHandle hConnect(WinHttpConnect(hSession.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!hConnect)
        return false;

    WinHttpHandle hRequest(WinHttpOpenRequest(
        hConnect.get(), L"POST", path, NULL,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE
    ));
    if (!hRequest)
        return false;

    wchar_t headers[2560];
    swprintf(headers, 2560, L"Content-Type: application/json\r\napikey: %ls\r\n", apikey.c_str());

    const int bodyLen = static_cast<int>(jsonBody.size());

    BOOL sent = WinHttpSendRequest(
        hRequest.get(), headers, static_cast<DWORD>(-1),
        const_cast<LPVOID>(static_cast<const void*>(jsonBody.c_str())),
        static_cast<DWORD>(bodyLen), static_cast<DWORD>(bodyLen), 0
    );

    if (!sent || !WinHttpReceiveResponse(hRequest.get(), NULL))
        return false;

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

        DWORD remaining = static_cast<DWORD>(responseOutSize) - 1 - totalRead;
        if (available > remaining)
            available = remaining;
        if (available == 0)
            break;

        DWORD bytesRead = 0;
        if (!WinHttpReadData(hRequest.get(), responseOut + totalRead, available, &bytesRead) || bytesRead == 0)
            break;

        totalRead += bytesRead;
    }

    responseOut[totalRead] = '\0';
    *statusOut = status;
    return true;
}

std::string sessionFilePath()
{
    char dir[MAX_PATH];
    settings_get_config_dir(dir, sizeof(dir));
    return std::string(dir) + "\\session.dat";
}

/*
 * Lưu session xuống %APPDATA%\JustInTime\session.dat, mã hóa bằng
 * DPAPI (CryptProtectData) - chỉ tài khoản Windows hiện tại mới giải
 * mã lại được.
 */
bool saveSessionToDisk(const AuthSession& s)
{
    char buf[8192];
    int len = snprintf(buf, sizeof(buf), "%s\n%s\n%s\n%s\n", s.user_id, s.email, s.access_token, s.refresh_token);
    if (len <= 0)
        return false;

    DATA_BLOB inBlob;
    inBlob.pbData = reinterpret_cast<BYTE*>(buf);
    inBlob.cbData = static_cast<DWORD>(len);

    DATA_BLOB outBlob = {0};

    if (!CryptProtectData(&inBlob, L"JustInTime session", NULL, NULL, NULL, 0, &outBlob))
        return false;

    LocalAllocGuard guard(outBlob.pbData);

    std::ofstream f(sessionFilePath(), std::ios::out | std::ios::binary | std::ios::trunc);
    if (!f)
        return false;

    f.write(reinterpret_cast<const char*>(outBlob.pbData), outBlob.cbData);
    return f.good();
}

bool loadSessionFromDisk(AuthSession* s)
{
    std::ifstream f(sessionFilePath(), std::ios::in | std::ios::binary);
    if (!f)
        return false;

    std::vector<unsigned char> enc(
        (std::istreambuf_iterator<char>(f)),
        std::istreambuf_iterator<char>()
    );
    if (enc.empty())
        return false;

    DATA_BLOB inBlob;
    inBlob.pbData = enc.data();
    inBlob.cbData = static_cast<DWORD>(enc.size());

    DATA_BLOB outBlob = {0};

    if (!CryptUnprotectData(&inBlob, NULL, NULL, NULL, NULL, 0, &outBlob))
        return false;

    LocalAllocGuard guard(outBlob.pbData);

    char buf[8192] = {0};
    DWORD copyLen = outBlob.cbData < sizeof(buf) - 1 ? outBlob.cbData : sizeof(buf) - 1;
    memcpy(buf, outBlob.pbData, copyLen);
    buf[copyLen] = '\0';

    char* line1 = strtok(buf, "\n");
    char* line2 = line1 ? strtok(NULL, "\n") : nullptr;
    char* line3 = line2 ? strtok(NULL, "\n") : nullptr;
    char* line4 = line3 ? strtok(NULL, "\n") : nullptr;

    if (!line1 || !line2 || !line3 || !line4)
        return false;

    snprintf(s->user_id, sizeof(s->user_id), "%s", line1);
    snprintf(s->email, sizeof(s->email), "%s", line2);
    snprintf(s->access_token, sizeof(s->access_token), "%s", line3);
    snprintf(s->refresh_token, sizeof(s->refresh_token), "%s", line4);

    s->logged_in = 1;
    return true;
}

int doAuthFlow(
    const char* endpointPath,
    const char* email,
    const char* password,
    char* errOut, int errOutSize)
{
    char url[MAX_URL_LEN] = {0};
    char key[MAX_KEY_LEN] = {0};
    settings_get_supabase_config(url, sizeof(url), key, sizeof(key));

    const std::string emailEsc = jsonEscape(email);
    const std::string passwordEsc = jsonEscape(password);

    const std::string body = "{\"email\":\"" + emailEsc + "\",\"password\":\"" + passwordEsc + "\"}";
    const std::wstring wpath = utf8ToWide(endpointPath);

    char response[4096] = {0};
    DWORD status = 0;

    if (!doAuthPost(url, key, wpath.c_str(), body, response, sizeof(response), &status))
    {
        snprintf(errOut, errOutSize, "[%s] Could not connect to Supabase", ERR_AUTH_NETWORK);
        return 0;
    }

    if (status < 200 || status >= 300)
    {
        char msg[512] = {0};

        if (
            !json_extract_string(response, "error_description", msg, sizeof(msg)) &&
            !json_extract_string(response, "msg", msg, sizeof(msg)) &&
            !json_extract_string(response, "message", msg, sizeof(msg))
        )
            snprintf(msg, sizeof(msg), "Unknown error (HTTP %lu)", status);

        snprintf(errOut, errOutSize, "[%s] %s", ERR_AUTH_SERVER_REJECT, msg);
        return 0;
    }

    AuthSession s;
    memset(&s, 0, sizeof(s));

    if (json_extract_string(response, "access_token", s.access_token, sizeof(s.access_token)))
    {
        json_extract_string(response, "refresh_token", s.refresh_token, sizeof(s.refresh_token));
        json_extract_string(response, "id", s.user_id, sizeof(s.user_id));
        json_extract_string(response, "email", s.email, sizeof(s.email));
        s.logged_in = 1;

        {
            std::lock_guard<std::mutex> lock(sessionMutex());
            g_session = s;
        }

        saveSessionToDisk(s);
        return 1;
    }

    /*
     * Không có access_token trong response (thường gặp khi đăng ký
     * và project yêu cầu xác nhận email).
     */
    snprintf(errOut, errOutSize, "Success, but you need to confirm your email before logging in.");
    return 1;
}

} // namespace

int auth_register(
    const char* email,
    const char* password,
    char* err_out,
    int err_out_size)
{
    return doAuthFlow("/auth/v1/signup", email, password, err_out, err_out_size);
}

int auth_login(
    const char* email,
    const char* password,
    char* err_out,
    int err_out_size)
{
    return doAuthFlow("/auth/v1/token?grant_type=password", email, password, err_out, err_out_size);
}

void auth_logout(void)
{
    {
        std::lock_guard<std::mutex> lock(sessionMutex());
        memset(&g_session, 0, sizeof(g_session));
    }

    DeleteFileA(sessionFilePath().c_str());
}

int auth_refresh_session(void)
{
    std::string refreshToken;
    {
        std::lock_guard<std::mutex> lock(sessionMutex());
        refreshToken = g_session.refresh_token;
    }

    if (refreshToken.empty())
    {
        /*
         * Chưa từng đăng nhập, hoặc session trong bộ nhớ đang trống
         * (vd auth_load_session() thất bại) - không có gì để refresh.
         */
        return 0;
    }

    char url[MAX_URL_LEN] = {0};
    char key[MAX_KEY_LEN] = {0};
    settings_get_supabase_config(url, sizeof(url), key, sizeof(key));

    const std::string refreshEsc = jsonEscape(refreshToken);
    const std::string body = "{\"refresh_token\":\"" + refreshEsc + "\"}";

    char response[4096] = {0};
    DWORD status = 0;

    if (
        !doAuthPost(
            url, key, L"/auth/v1/token?grant_type=refresh_token",
            body, response, sizeof(response), &status
        )
    )
    {
        wprintf(L"[AUTH][%hs] Khong the ket noi de refresh token\n", ERR_AUTH_NETWORK);
        return 0;
    }

    if (status < 200 || status >= 300)
    {
        /*
         * refresh_token cũng đã hết hạn/bị thu hồi (hoặc bị vô hiệu
         * do project đổi JWT signing key) - phải đăng nhập lại thủ
         * công. Xoá session cũ để tránh cứ thử refresh 1 token chắc
         * chắn hỏng mỗi vòng sync tiếp theo.
         */
        wprintf(L"[AUTH][%hs] Refresh token bi tu choi (HTTP %lu): %hs\n", ERR_AUTH_REFRESH_FAIL, status, response);
        auth_logout();
        return 0;
    }

    AuthSession s;
    memset(&s, 0, sizeof(s));

    if (!json_extract_string(response, "access_token", s.access_token, sizeof(s.access_token)))
    {
        wprintf(L"[AUTH][%hs] Response refresh khong co access_token\n", ERR_AUTH_REFRESH_FAIL);
        return 0;
    }

    json_extract_string(response, "refresh_token", s.refresh_token, sizeof(s.refresh_token));
    json_extract_string(response, "id", s.user_id, sizeof(s.user_id));
    json_extract_string(response, "email", s.email, sizeof(s.email));

    {
        std::lock_guard<std::mutex> lock(sessionMutex());

        /*
         * Một số phiên bản GoTrue không trả lại user_id/email trong
         * response refresh (chỉ access_token/refresh_token) - giữ
         * nguyên giá trị cũ trong trường hợp đó thay vì ghi đè bằng
         * chuỗi rỗng.
         */
        if (s.user_id[0] == '\0')
            snprintf(s.user_id, sizeof(s.user_id), "%s", g_session.user_id);
        if (s.email[0] == '\0')
            snprintf(s.email, sizeof(s.email), "%s", g_session.email);

        /*
         * Nếu Supabase không cấp refresh_token mới trong response
         * (một số cấu hình vẫn dùng lại refresh_token cũ), giữ lại
         * cái đang có thay vì xoá mất.
         */
        if (s.refresh_token[0] == '\0')
            snprintf(s.refresh_token, sizeof(s.refresh_token), "%s", g_session.refresh_token);

        s.logged_in = 1;
        g_session = s;
    }

    saveSessionToDisk(s);
    wprintf(L"[AUTH] Refresh token thanh cong, da co access_token moi\n");
    return 1;
}

int auth_load_session(void)
{
    AuthSession s;
    memset(&s, 0, sizeof(s));

    if (!loadSessionFromDisk(&s))
        return 0;

    std::lock_guard<std::mutex> lock(sessionMutex());
    g_session = s;
    return 1;
}

void auth_get_session(AuthSession* out)
{
    std::lock_guard<std::mutex> lock(sessionMutex());
    *out = g_session;
}

int auth_is_logged_in(void)
{
    std::lock_guard<std::mutex> lock(sessionMutex());
    return g_session.logged_in;
}
