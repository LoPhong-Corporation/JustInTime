//
// auth.c
//

#include "../include/auth.h"
#include "../include/settings.h"
#include "../include/jsonutil.h"
#include "../include/error_codes.h"

#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "crypt32.lib")

static AuthSession       g_session;
static CRITICAL_SECTION  g_session_lock;
static int               g_session_lock_ready = 0;

static void ensure_lock(void)
{
    if (!g_session_lock_ready)
    {
        InitializeCriticalSection(&g_session_lock);
        g_session_lock_ready = 1;
    }
}

/*
 * Trích 1 trường string đơn giản dạng "key":"value" từ
 * JSON phẳng (không xử lý JSON lồng phức tạp, chỉ đủ dùng
 * cho response của Supabase Auth).
 */
static int json_extract_string(
    const char* json,
    const char* key,
    char* out,
    int out_size)
{
    char pattern[128];

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char* pos = strstr(json, pattern);

    if (!pos)
        return 0;

    pos = strchr(pos + strlen(pattern), ':');

    if (!pos)
        return 0;

    pos++;

    while (*pos == ' ')
        pos++;

    if (*pos != '"')
        return 0;

    pos++;

    int i = 0;

    while (*pos && *pos != '"' && i < out_size - 1)
    {
        if (*pos == '\\' && *(pos + 1) != '\0')
        {
            pos++;
            out[i++] = *pos;
        }
        else
        {
            out[i++] = *pos;
        }

        pos++;
    }

    out[i] = '\0';

    return 1;
}

static void extract_host(
    const char* url,
    wchar_t* host_out,
    int host_out_size)
{
    const char* p = strstr(url, "://");
    const char* host = p ? p + 3 : url;

    MultiByteToWideChar(
        CP_UTF8, 0,
        host, -1,
        host_out, host_out_size
    );
}

/*
 * Gửi POST request tới 1 endpoint của Supabase Auth
 * (GoTrue), trả về status code + toàn bộ response body.
 */
static int do_auth_post(
    const char* base_url,
    const char* apikey_str,
    const wchar_t* path,
    const char* json_body,
    char* response_out,
    int response_out_size,
    DWORD* status_out)
{
    wchar_t host[256] = {0};

    extract_host(base_url, host, 256);

    wchar_t apikey[2048] = {0};

    MultiByteToWideChar(
        CP_UTF8, 0,
        apikey_str, -1,
        apikey, 2048
    );

    int ok = 0;

    HINTERNET hSession = WinHttpOpen(
        L"JustInTime-Agent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!hSession)
        return 0;

    HINTERNET hConnect = WinHttpConnect(
        hSession,
        host,
        INTERNET_DEFAULT_HTTPS_PORT,
        0
    );

    if (hConnect)
    {
        HINTERNET hRequest = WinHttpOpenRequest(
            hConnect,
            L"POST",
            path,
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        );

        if (hRequest)
        {
            wchar_t headers[2560];

            swprintf(
                headers,
                2560,
                L"Content-Type: application/json\r\n"
                L"apikey: %ls\r\n",
                apikey
            );

            int body_len = (int)strlen(json_body);

            BOOL sent = WinHttpSendRequest(
                hRequest,
                headers,
                (DWORD)-1,
                (LPVOID)json_body,
                (DWORD)body_len,
                (DWORD)body_len,
                0
            );

            if (sent && WinHttpReceiveResponse(hRequest, NULL))
            {
                DWORD status = 0;
                DWORD status_size = sizeof(status);

                WinHttpQueryHeaders(
                    hRequest,
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

                    if (
                        !WinHttpQueryDataAvailable(hRequest, &available)
                        || available == 0
                    )
                        break;

                    if (available > (DWORD)response_out_size - 1 - total_read)
                        available = (DWORD)response_out_size - 1 - total_read;

                    if (available == 0)
                        break;

                    DWORD bytes_read = 0;

                    if (
                        !WinHttpReadData(
                            hRequest,
                            response_out + total_read,
                            available,
                            &bytes_read
                        )
                        || bytes_read == 0
                    )
                        break;

                    total_read += bytes_read;
                }

                response_out[total_read] = '\0';

                *status_out = status;
                ok = 1;
            }

            WinHttpCloseHandle(hRequest);
        }

        WinHttpCloseHandle(hConnect);
    }

    WinHttpCloseHandle(hSession);

    return ok;
}

/*
 * Lưu session xuống %APPDATA%\JustInTime\session.dat,
 * mã hóa bằng DPAPI (CryptProtectData) - chỉ tài khoản
 * Windows hiện tại mới giải mã lại được.
 */
static int save_session_to_disk(const AuthSession* s)
{
    char buf[8192];

    int len = snprintf(
        buf,
        sizeof(buf),
        "%s\n%s\n%s\n%s\n",
        s->user_id,
        s->email,
        s->access_token,
        s->refresh_token
    );

    if (len <= 0)
        return 0;

    DATA_BLOB in_blob;
    in_blob.pbData = (BYTE*)buf;
    in_blob.cbData = (DWORD)len;

    DATA_BLOB out_blob = {0};

    if (
        !CryptProtectData(
            &in_blob,
            L"JustInTime session",
            NULL,
            NULL,
            NULL,
            0,
            &out_blob
        )
    )
        return 0;

    char dir[MAX_PATH];
    char path[MAX_PATH];

    settings_get_config_dir(dir, sizeof(dir));

    snprintf(
        path,
        sizeof(path),
        "%s\\session.dat",
        dir
    );

    FILE* f = fopen(path, "wb");

    if (!f)
    {
        LocalFree(out_blob.pbData);
        return 0;
    }

    fwrite(out_blob.pbData, 1, out_blob.cbData, f);
    fclose(f);

    LocalFree(out_blob.pbData);

    return 1;
}

static int load_session_from_disk(AuthSession* s)
{
    char dir[MAX_PATH];
    char path[MAX_PATH];

    settings_get_config_dir(dir, sizeof(dir));

    snprintf(
        path,
        sizeof(path),
        "%s\\session.dat",
        dir
    );

    FILE* f = fopen(path, "rb");

    if (!f)
        return 0;

    unsigned char enc[8192];

    size_t enc_len = fread(enc, 1, sizeof(enc), f);

    fclose(f);

    if (enc_len == 0)
        return 0;

    DATA_BLOB in_blob;
    in_blob.pbData = enc;
    in_blob.cbData = (DWORD)enc_len;

    DATA_BLOB out_blob = {0};

    if (
        !CryptUnprotectData(
            &in_blob,
            NULL,
            NULL,
            NULL,
            NULL,
            0,
            &out_blob
        )
    )
        return 0;

    char buf[8192] = {0};

    DWORD copy_len =
        out_blob.cbData < sizeof(buf) - 1
        ? out_blob.cbData
        : sizeof(buf) - 1;

    memcpy(buf, out_blob.pbData, copy_len);
    buf[copy_len] = '\0';

    LocalFree(out_blob.pbData);

    char* line1 = strtok(buf, "\n");
    char* line2 = strtok(NULL, "\n");
    char* line3 = strtok(NULL, "\n");
    char* line4 = strtok(NULL, "\n");

    if (!line1 || !line2 || !line3 || !line4)
        return 0;

    snprintf(s->user_id, sizeof(s->user_id), "%s", line1);
    snprintf(s->email, sizeof(s->email), "%s", line2);
    snprintf(s->access_token, sizeof(s->access_token), "%s", line3);
    snprintf(s->refresh_token, sizeof(s->refresh_token), "%s", line4);

    s->logged_in = 1;

    return 1;
}

static int do_auth_flow(
    const char* endpoint_path,
    const char* email,
    const char* password,
    char* err_out,
    int err_out_size)
{
    char url[MAX_URL_LEN]  = {0};
    char key[MAX_KEY_LEN]  = {0};

    settings_get_supabase_config(
        url, sizeof(url),
        key, sizeof(key)
    );

    char email_esc[512]    = {0};
    char password_esc[512] = {0};

    json_escape(email, email_esc, sizeof(email_esc));
    json_escape(password, password_esc, sizeof(password_esc));

    char body[1200];

    snprintf(
        body,
        sizeof(body),
        "{\"email\":\"%s\",\"password\":\"%s\"}",
        email_esc,
        password_esc
    );

    wchar_t wpath[128] = {0};

    MultiByteToWideChar(
        CP_UTF8, 0,
        endpoint_path, -1,
        wpath, 128
    );

    char response[4096] = {0};
    DWORD status = 0;

    if (
        !do_auth_post(
            url, key,
            wpath,
            body,
            response,
            sizeof(response),
            &status
        )
    )
    {
        snprintf(
            err_out,
            err_out_size,
            "[%s] Could not connect to Supabase",
            ERR_AUTH_NETWORK
        );

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
        {
            snprintf(msg, sizeof(msg), "Unknown error (HTTP %lu)", status);
        }

        snprintf(
            err_out,
            err_out_size,
            "[%s] %s",
            ERR_AUTH_SERVER_REJECT,
            msg
        );

        return 0;
    }

    AuthSession s;
    memset(&s, 0, sizeof(s));

    if (
        json_extract_string(response, "access_token", s.access_token, sizeof(s.access_token))
    )
    {
        json_extract_string(response, "refresh_token", s.refresh_token, sizeof(s.refresh_token));
        json_extract_string(response, "id", s.user_id, sizeof(s.user_id));
        json_extract_string(response, "email", s.email, sizeof(s.email));

        s.logged_in = 1;

        ensure_lock();

        EnterCriticalSection(&g_session_lock);
        g_session = s;
        LeaveCriticalSection(&g_session_lock);

        save_session_to_disk(&s);

        return 1;
    }

    /*
     * Không có access_token trong response (thường gặp
     * khi đăng ký và project yêu cầu xác nhận email).
     */
    snprintf(
        err_out,
        err_out_size,
        "Success, but you need to confirm your email before logging in."
    );

    return 1;
}

int auth_register(
    const char* email,
    const char* password,
    char* err_out,
    int err_out_size)
{
    return do_auth_flow(
        "/auth/v1/signup",
        email,
        password,
        err_out,
        err_out_size
    );
}

int auth_login(
    const char* email,
    const char* password,
    char* err_out,
    int err_out_size)
{
    return do_auth_flow(
        "/auth/v1/token?grant_type=password",
        email,
        password,
        err_out,
        err_out_size
    );
}

void auth_logout(void)
{
    ensure_lock();

    EnterCriticalSection(&g_session_lock);
    memset(&g_session, 0, sizeof(g_session));
    LeaveCriticalSection(&g_session_lock);

    char dir[MAX_PATH];
    char path[MAX_PATH];

    settings_get_config_dir(dir, sizeof(dir));

    snprintf(
        path,
        sizeof(path),
        "%s\\session.dat",
        dir
    );

    DeleteFileA(path);
}

int auth_refresh_session(void)
{
    ensure_lock();

    char refresh_token[512] = {0};

    EnterCriticalSection(&g_session_lock);
    snprintf(refresh_token, sizeof(refresh_token), "%s", g_session.refresh_token);
    LeaveCriticalSection(&g_session_lock);

    if (refresh_token[0] == '\0')
    {
        /*
         * Chưa từng đăng nhập, hoặc session trong bộ nhớ
         * đang trống (vd auth_load_session() thất bại) -
         * không có gì để refresh.
         */
        return 0;
    }

    char url[MAX_URL_LEN] = {0};
    char key[MAX_KEY_LEN] = {0};

    settings_get_supabase_config(
        url, sizeof(url),
        key, sizeof(key)
    );

    char refresh_esc[600] = {0};

    json_escape(refresh_token, refresh_esc, sizeof(refresh_esc));

    char body[700];

    snprintf(
        body,
        sizeof(body),
        "{\"refresh_token\":\"%s\"}",
        refresh_esc
    );

    char response[4096] = {0};
    DWORD status = 0;

    if (
        !do_auth_post(
            url, key,
            L"/auth/v1/token?grant_type=refresh_token",
            body,
            response,
            sizeof(response),
            &status
        )
    )
    {
        wprintf(L"[AUTH][%hs] Khong the ket noi de refresh token\n", ERR_AUTH_NETWORK);
        return 0;
    }

    if (status < 200 || status >= 300)
    {
        /*
         * refresh_token cũng đã hết hạn/bị thu hồi (hoặc bị
         * vô hiệu do project đổi JWT signing key) - phải
         * đăng nhập lại thủ công. Xoá session cũ để tránh
         * cứ thử refresh một token chắc chắn hỏng mỗi vòng
         * sync tiếp theo.
         */
        wprintf(
            L"[AUTH][%hs] Refresh token bi tu choi (HTTP %lu): %hs\n",
            ERR_AUTH_REFRESH_FAIL,
            status,
            response
        );

        auth_logout();

        return 0;
    }

    AuthSession s;
    memset(&s, 0, sizeof(s));

    if (
        !json_extract_string(response, "access_token", s.access_token, sizeof(s.access_token))
    )
    {
        wprintf(L"[AUTH][%hs] Response refresh khong co access_token\n", ERR_AUTH_REFRESH_FAIL);
        return 0;
    }

    json_extract_string(response, "refresh_token", s.refresh_token, sizeof(s.refresh_token));
    json_extract_string(response, "id", s.user_id, sizeof(s.user_id));
    json_extract_string(response, "email", s.email, sizeof(s.email));

    ensure_lock();

    EnterCriticalSection(&g_session_lock);

    /*
     * Một số phiên bản GoTrue không trả lại user_id/email
     * trong response refresh (chỉ access_token/refresh_token) -
     * giữ nguyên giá trị cũ trong trường hợp đó thay vì ghi đè
     * bằng chuỗi rỗng.
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

    LeaveCriticalSection(&g_session_lock);

    save_session_to_disk(&s);

    wprintf(L"[AUTH] Refresh token thanh cong, da co access_token moi\n");

    return 1;
}

int auth_load_session(void)
{
    ensure_lock();

    AuthSession s;
    memset(&s, 0, sizeof(s));

    if (!load_session_from_disk(&s))
        return 0;

    EnterCriticalSection(&g_session_lock);
    g_session = s;
    LeaveCriticalSection(&g_session_lock);

    return 1;
}

void auth_get_session(AuthSession* out)
{
    ensure_lock();

    EnterCriticalSection(&g_session_lock);
    *out = g_session;
    LeaveCriticalSection(&g_session_lock);
}

int auth_is_logged_in(void)
{
    ensure_lock();

    int logged_in;

    EnterCriticalSection(&g_session_lock);
    logged_in = g_session.logged_in;
    LeaveCriticalSection(&g_session_lock);

    return logged_in;
}
