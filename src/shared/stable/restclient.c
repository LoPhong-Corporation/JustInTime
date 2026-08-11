//
// restclient.c
//
// Chế độ STABLE (C core) - bản hiện thực gốc bằng C thuần, dùng
// WinHTTP trực tiếp với dọn dẹp HINTERNET thủ công ở từng nhánh.
// Interface (restclient.h) y hệt bản Experimental (C++, xem
// src/shared/experimental/restclient.cpp) - 2 bản triển khai này KHÔNG
// BAO GIỜ được biên dịch cùng lúc (CMakeLists.txt chọn 1 trong 2 tuỳ
// JUSTINTIME_CORE=STABLE|EXPERIMENTAL), nhưng có thể build thử cả 2 để
// so sánh.
//

#include "restclient.h"
#include "config.h"
#include "settings.h"
#include "auth.h"

#include <winhttp.h>

#include <stdio.h>
#include <string.h>

#pragma comment(lib, "winhttp.lib")

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

    /* Tách phần host ra khỏi "https://xxxx.supabase.co" */
    const char* host_start = strstr(base_url, "://");
    host_start = host_start ? host_start + 3 : base_url;

    wchar_t host[256] = {0};
    MultiByteToWideChar(CP_UTF8, 0, host_start, -1, host, 256);

    wchar_t apikey[2048] = {0};
    MultiByteToWideChar(CP_UTF8, 0, apikey_str, -1, apikey, 2048);

    wchar_t access_token[2048] = {0};
    MultiByteToWideChar(CP_UTF8, 0, session.access_token, -1, access_token, 2048);

    wchar_t method_w[16] = {0};
    MultiByteToWideChar(CP_UTF8, 0, method, -1, method_w, 16);

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

    if (!hConnect)
    {
        wprintf(L"[REST] WinHttpConnect that bai (%lu)\n", GetLastError());
        WinHttpCloseHandle(hSession);
        return 0;
    }

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        method_w,
        path,
        NULL,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE
    );

    if (!hRequest)
    {
        wprintf(L"[REST] WinHttpOpenRequest that bai (%lu)\n", GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
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

    if (!sent || !WinHttpReceiveResponse(hRequest, NULL))
    {
        wprintf(L"[REST] Gui request that bai (%lu)\n", GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return 0;
    }

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

        DWORD remaining = (DWORD)response_out_size - 1 - total_read;
        if (available > remaining)
            available = remaining;

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

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return 1;
}
