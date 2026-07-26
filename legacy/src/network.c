//
// Created by LoPhongCorporation on 6/24/2026.
// Rewritten: gui du lieu qua Supabase Edge Function
// "sync-activity" (thay vi goi thang PostgREST) de
// service_role key khong bao gio phai nam trong client.
//

#include "../include/network.h"
#include "../include/config.h"
#include "../include/jsonutil.h"
#include "../include/settings.h"
#include "../include/auth.h"
#include "../include/error_codes.h"

#include <windows.h>
#include <winhttp.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

#pragma comment(lib, "winhttp.lib")

/*
 * Tach phan host ra khoi SUPABASE_URL dang
 * "https://xxxx.supabase.co" -> "xxxx.supabase.co"
 */
static void extract_host(
    const char* url,
    wchar_t* host_out,
    int host_out_size)
{
    const char* p =
        strstr(url, "://");

    const char* host =
        p ? p + 3 : url;

    MultiByteToWideChar(
        CP_UTF8,
        0,
        host,
        -1,
        host_out,
        host_out_size
    );
}

/*
 * Chuyen mot SyncRecord thanh JSON body de POST len
 * bang activity_logs tren Supabase.
 * Khong gui "id" cuc bo, vi id nay chi co y nghia
 * trong SQLite cua tung may, khong phai khoa chung.
 */
static int build_json_body(
    const SyncRecord* rec,
    char* out,
    int out_size)
{
    char process_utf8[1024] = {0};
    char title_utf8[4096] = {0};

    WideCharToMultiByte(
        CP_UTF8, 0,
        rec->process_name, -1,
        process_utf8, sizeof(process_utf8),
        NULL, NULL
    );

    WideCharToMultiByte(
        CP_UTF8, 0,
        rec->window_title, -1,
        title_utf8, sizeof(title_utf8),
        NULL, NULL
    );

    char device_esc[256]   = {0};
    char process_esc[2048] = {0};
    char title_esc[8192]   = {0};

    json_escape(rec->device_id, device_esc, sizeof(device_esc));
    json_escape(process_utf8,   process_esc, sizeof(process_esc));
    json_escape(title_utf8,     title_esc,   sizeof(title_esc));

    return snprintf(
        out,
        out_size,
        "{"
        "\"device_id\":\"%s\","
        "\"process_name\":\"%s\","
        "\"window_title\":\"%s\","
        "\"duration_seconds\":%ld,"
        "\"start_time\":%lld,"
        "\"end_time\":%lld"
        "}",
        device_esc,
        process_esc,
        title_esc,
        rec->duration_seconds,
        rec->start_time,
        rec->end_time
    );
}

/*
 * Thực hiện đúng 1 lần gọi HTTP POST tới Edge Function
 * sync-activity với access_token truyền vào. Tách riêng để
 * network_send_record() có thể gọi lại lần 2 sau khi refresh
 * token, mà không phải lặp lại toàn bộ code dựng request.
 *
 * Trả về 1 nếu request được gửi/nhận phản hồi thành công
 * (bất kể status HTTP là gì); *status_out sẽ chứa mã HTTP,
 * và resp_out chứa body phản hồi (để log khi có lỗi).
 * Trả về 0 nếu bản thân request thất bại ở tầng WinHTTP
 * (không kết nối được, DNS lỗi, v.v.).
 */
static int try_send_record(
    const char* body,
    int body_len,
    const char* access_token_str,
    DWORD* status_out,
    char* resp_out,
    int resp_out_size)
{
    char base_url[MAX_URL_LEN] = {0};
    char apikey_str[MAX_KEY_LEN] = {0};

    settings_get_supabase_config(
        base_url, sizeof(base_url),
        apikey_str, sizeof(apikey_str)
    );

    wchar_t host[256] = {0};

    extract_host(
        base_url,
        host,
        256
    );

    wchar_t apikey[2048] = {0};

    MultiByteToWideChar(
        CP_UTF8, 0,
        apikey_str, -1,
        apikey, 2048
    );

    wchar_t access_token[MAX_TOKEN_LEN] = {0};

    MultiByteToWideChar(
        CP_UTF8, 0,
        access_token_str, -1,
        access_token, MAX_TOKEN_LEN
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
    {
        wprintf(L"[SYNC] WinHttpOpen that bai (%lu)\n", GetLastError());
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
        const wchar_t* path = L"/functions/v1/sync-activity";

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
            wchar_t headers[4096];

            swprintf(
                headers,
                4096,
                L"Content-Type: application/json\r\n"
                L"apikey: %ls\r\n"
                L"Authorization: Bearer %ls\r\n",
                apikey,
                access_token
            );

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

                    if (
                        !WinHttpQueryDataAvailable(hRequest, &available)
                        || available == 0
                    )
                        break;

                    if (available > (DWORD)resp_out_size - 1 - total_read)
                        available = (DWORD)resp_out_size - 1 - total_read;

                    if (available == 0)
                        break;

                    DWORD bytes_read = 0;

                    if (
                        !WinHttpReadData(
                            hRequest,
                            resp_out + total_read,
                            available,
                            &bytes_read
                        )
                        || bytes_read == 0
                    )
                        break;

                    total_read += bytes_read;
                }

                resp_out[total_read] = '\0';

                *status_out = status;
                ok = 1;
            }
            else
            {
                wprintf(
                    L"[SYNC][%hs] Gui request that bai (%lu)\n",
                    ERR_SYNC_CONNECT_FAIL,
                    GetLastError()
                );
            }

            WinHttpCloseHandle(hRequest);
        }
        else
        {
            wprintf(
                L"[SYNC][%hs] WinHttpOpenRequest that bai (%lu)\n",
                ERR_SYNC_CONNECT_FAIL,
                GetLastError()
            );
        }

        WinHttpCloseHandle(hConnect);
    }
    else
    {
        wprintf(
            L"[SYNC][%hs] WinHttpConnect that bai (%lu)\n",
            ERR_SYNC_CONNECT_FAIL,
            GetLastError()
        );
    }

    WinHttpCloseHandle(hSession);

    return ok;
}

/*
 * Gui mot record len Edge Function "sync-activity" qua HTTPS.
 * Function ben server se dung service_role key (khong lo ra
 * client) de upsert vao bang activity_logs, dua tren
 * (device_id, start_time) de tranh trung du lieu neu record
 * da duoc gui thanh cong truoc do nhung chua kip danh dau
 * "synced" cuc bo.
 *
 * Yeu cau: da deploy Edge Function sync-activity, va bang
 * activity_logs co UNIQUE constraint tren (device_id, start_time).
 *
 * Neu Supabase tra ve 401 (access_token het han - vi du loi
 * "UNAUTHORIZED_ASYMMETRIC_JWT" / "Invalid JWT"), tu dong thu
 * refresh token 1 lan roi gui lai, thay vi that bai lien tuc
 * cho toi khi nguoi dung tu dang xuat/dang nhap lai.
 */
int network_send_record(
    const SyncRecord* rec)
{
    if (!rec)
        return 0;

    /*
     * Bắt buộc phải đăng nhập mới sync lên cloud được,
     * vì Edge Function cần access_token thật của user để
     * biết record này thuộc về ai (user_id).
     * Dữ liệu vẫn được lưu đầy đủ ở local (SQLite + backup
     * JSON) dù chưa đăng nhập, chỉ là chưa lên được cloud.
     */
    if (!auth_is_logged_in())
    {
        return 0;
    }

    AuthSession session;
    auth_get_session(&session);

    char body[16384] = {0};

    int body_len =
        build_json_body(
            rec,
            body,
            sizeof(body)
        );

    if (body_len <= 0 || body_len >= (int)sizeof(body))
    {
        wprintf(L"[SYNC][%hs] Khong the tao JSON body (qua dai hoac loi)\n", ERR_SYNC_PAYLOAD_TOO_BIG);
        return 0;
    }

    DWORD status = 0;
    char resp_body[4096] = {0};

    if (!try_send_record(body, body_len, session.access_token, &status, resp_body, sizeof(resp_body)))
    {
        /* Loi tang WinHTTP (khong ket noi duoc, v.v.) - try_send_record da tu log. */
        return 0;
    }

    if (status >= 200 && status < 300)
    {
        return 1;
    }

    if (status == 401)
    {
        wprintf(
            L"[SYNC][%hs] Supabase tra ve loi HTTP 401 (access_token co the da het han): %hs\n"
            L"[SYNC] Dang thu refresh token...\n",
            ERR_SYNC_SERVER_ERROR,
            resp_body
        );

        if (auth_refresh_session())
        {
            AuthSession refreshed;
            auth_get_session(&refreshed);

            DWORD retry_status = 0;
            char retry_resp[4096] = {0};

            if (
                try_send_record(body, body_len, refreshed.access_token, &retry_status, retry_resp, sizeof(retry_resp))
                && retry_status >= 200 && retry_status < 300
            )
            {
                wprintf(L"[SYNC] Gui lai sau khi refresh token thanh cong\n");
                return 1;
            }

            wprintf(
                L"[SYNC][%hs] Van that bai sau khi refresh token (HTTP %lu): %hs\n",
                ERR_SYNC_SERVER_ERROR,
                retry_status,
                retry_resp
            );
        }
        else
        {
            wprintf(
                L"[SYNC][%hs] Refresh token that bai - can dang nhap lai qua tray\n",
                ERR_AUTH_REFRESH_FAIL
            );
        }

        return 0;
    }

    wprintf(
        L"[SYNC][%hs] Supabase tra ve loi HTTP %lu: %hs\n",
        ERR_SYNC_SERVER_ERROR,
        status,
        resp_body
    );

    return 0;
}
