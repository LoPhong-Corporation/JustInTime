//
// restclient.c
//

#include "restclient.h"
#include "config.h"
#include "settings.h"
#include "auth.h"

#include <winhttp.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "winhttp.lib")

/*
 * Tach phan host ra khoi SUPABASE_URL dang
 * "https://xxxx.supabase.co" -> "xxxx.supabase.co"
 * (bản riêng của module này - network.c có 1 bản tương tự
 * nhưng để static riêng cho từng file, không đáng để gộp
 * chung 1 hàm 15 dòng qua 1 header mới).
 */
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

int restclient_call(
    const char* method,
    const wchar_t* path,
    const char* body,
    const wchar_t* extra_headers,
    char* response_out, int response_out_size,
    DWORD* status_out)
{
    if (!method || !path || !response_out || !status_out)
        return 0;

    AuthSession session;
    auth_get_session(&session);

    char base_url[MAX_URL_LEN] = {0};
    char apikey_str[MAX_KEY_LEN] = {0};

    settings_get_supabase_config(
        base_url, sizeof(base_url),
        apikey_str, sizeof(apikey_str)
    );

    wchar_t host[256] = {0};
    extract_host(base_url, host, 256);

    wchar_t apikey[2048] = {0};
    MultiByteToWideChar(CP_UTF8, 0, apikey_str, -1, apikey, 2048);

    wchar_t access_token[MAX_TOKEN_LEN] = {0};
    MultiByteToWideChar(CP_UTF8, 0, session.access_token, -1, access_token, MAX_TOKEN_LEN);

    wchar_t method_w[16] = {0};
    MultiByteToWideChar(CP_UTF8, 0, method, -1, method_w, 16);

    int ok = 0;

    HINTERNET hSession = WinHttpOpen(
        L"JustInTime-Agent/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0
    );

    if (!hSession)
    {
        wprintf(L"[REST] WinHttpOpen that bai (%lu)\n", GetLastError());
        return 0;
    }

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
            method_w,
            path,
            NULL,
            WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            WINHTTP_FLAG_SECURE
        );

        if (hRequest)
        {
            wchar_t headers[4096];

            swprintf(
                headers,
                4096,
                L"Content-Type: application/json\r\n"
                L"apikey: %ls\r\n"
                L"Authorization: Bearer %ls\r\n"
                L"%ls",
                apikey,
                access_token,
                extra_headers ? extra_headers : L""
            );

            int body_len = body ? (int)strlen(body) : 0;

            BOOL sent = WinHttpSendRequest(
                hRequest,
                headers,
                (DWORD)-1,
                (LPVOID)body,
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

                    if (!WinHttpQueryDataAvailable(hRequest, &available) || available == 0)
                        break;

                    if (available > (DWORD)response_out_size - 1 - total_read)
                        available = (DWORD)response_out_size - 1 - total_read;

                    if (available == 0)
                        break;

                    DWORD bytes_read = 0;

                    if (
                        !WinHttpReadData(hRequest, response_out + total_read, available, &bytes_read)
                        || bytes_read == 0
                    )
                        break;

                    total_read += bytes_read;
                }

                response_out[total_read] = '\0';

                *status_out = status;
                ok = 1;
            }
            else
            {
                wprintf(L"[REST] Gui request that bai (%lu)\n", GetLastError());
            }

            WinHttpCloseHandle(hRequest);
        }
        else
        {
            wprintf(L"[REST] WinHttpOpenRequest that bai (%lu)\n", GetLastError());
        }

        WinHttpCloseHandle(hConnect);
    }
    else
    {
        wprintf(L"[REST] WinHttpConnect that bai (%lu)\n", GetLastError());
    }

    WinHttpCloseHandle(hSession);

    return ok;
}
