//
// remoteview.c
//

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "../include/remoteview.h"
#include "../include/settings.h"
#include "../include/database.h"
#include "../include/activity.h"
#include "../include/error_codes.h"

/*
 * QUAN TRỌNG: phải include winsock2.h TRƯỚC windows.h,
 * nếu không windows.h sẽ tự kéo winsock1.h vào gây xung
 * đột định nghĩa (lỗi kinh điển khi trộn Winsock + Windows.h).
 */
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

#pragma comment(lib, "ws2_32.lib")

static SOCKET  g_listen_socket = INVALID_SOCKET;
static HANDLE  g_thread = NULL;
static volatile int g_running = 0;

static void send_response(
    SOCKET client,
    int status_code,
    const char* status_text,
    const char* body)
{
    char header[512];
    int body_len = (int)strlen(body);

    snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        status_code, status_text, body_len
    );

    send(client, header, (int)strlen(header), 0);
    send(client, body, body_len, 0);
}

/*
 * Trích query param đơn giản dạng ?token=xxx từ path.
 * Không cần parser HTTP đầy đủ vì server này chỉ phục vụ
 * vài endpoint GET cố định, không nhận input phức tạp.
 */
static void extract_token(
    const char* path,
    char* out,
    int out_size)
{
    out[0] = '\0';

    const char* q = strstr(path, "token=");

    if (!q)
        return;

    q += 6;

    int i = 0;

    while (q[i] && q[i] != '&' && q[i] != ' ' && i < out_size - 1)
    {
        out[i] = q[i];
        i++;
    }

    out[i] = '\0';
}

static void json_escape_simple(
    const char* input,
    char* out,
    int out_size)
{
    int j = 0;

    for (int i = 0; input[i] != '\0' && j < out_size - 2; i++)
    {
        unsigned char c = (unsigned char)input[i];

        if (c == '"' || c == '\\')
        {
            out[j++] = '\\';
            out[j++] = (char)c;
        }
        else if (c >= 0x20)
        {
            out[j++] = (char)c;
        }
    }

    out[j] = '\0';
}

static void handle_client(SOCKET client)
{
    char request[4096] = {0};

    int received = recv(client, request, sizeof(request) - 1, 0);

    if (received <= 0)
    {
        closesocket(client);
        return;
    }

    request[received] = '\0';

    char method[16] = {0};
    char path[512]  = {0};

    sscanf(request, "%15s %511s", method, path);

    AppSettings s;
    settings_get(&s);

    char token[128] = {0};
    extract_token(path, token, sizeof(token));

    /*
     * Bắt buộc đúng token, và server phải đang thực sự
     * được bật trong settings (double-check, đề phòng
     * settings đổi giữa lúc server đang chạy).
     */
    if (
        strcmp(method, "GET") != 0 ||
        !s.remote_view_enabled ||
        s.remote_view_token[0] == '\0' ||
        strcmp(token, s.remote_view_token) != 0
    )
    {
        send_response(client, 401, "Unauthorized", "{\"error\":\"Invalid or missing token\"}");
        closesocket(client);
        return;
    }

    if (strncmp(path, "/status", 7) == 0)
    {
        wchar_t process_w[512]  = {0};
        wchar_t title_w[2048]   = {0};
        time_t  since = 0;

        activity_get_current(process_w, 512, title_w, 2048, &since);

        char process_utf8[1024] = {0};
        char title_utf8[4096]   = {0};

        WideCharToMultiByte(CP_UTF8, 0, process_w, -1, process_utf8, sizeof(process_utf8), NULL, NULL);
        WideCharToMultiByte(CP_UTF8, 0, title_w, -1, title_utf8, sizeof(title_utf8), NULL, NULL);

        char process_esc[1024] = {0};
        char title_esc[4096]   = {0};

        json_escape_simple(process_utf8, process_esc, sizeof(process_esc));
        json_escape_simple(title_utf8, title_esc, sizeof(title_esc));

        char body[8192];

        snprintf(
            body, sizeof(body),
            "{\"process_name\":\"%s\",\"window_title\":\"%s\",\"since\":%lld}",
            process_esc, title_esc, (long long)since
        );

        send_response(client, 200, "OK", body);
    }
    else if (strncmp(path, "/today", 6) == 0)
    {
        wchar_t summary_w[4096] = {0};

        db_build_daily_summary_text(summary_w, 4096);

        char summary_utf8[8192] = {0};

        WideCharToMultiByte(CP_UTF8, 0, summary_w, -1, summary_utf8, sizeof(summary_utf8), NULL, NULL);

        char summary_esc[8192] = {0};

        json_escape_simple(summary_utf8, summary_esc, sizeof(summary_esc));

        char body[9000];

        snprintf(body, sizeof(body), "{\"summary\":\"%s\"}", summary_esc);

        send_response(client, 200, "OK", body);
    }
    else
    {
        send_response(client, 404, "Not Found", "{\"error\":\"Unknown endpoint. Try /status or /today\"}");
    }

    closesocket(client);
}

static DWORD WINAPI accept_loop(LPVOID param)
{
    (void)param;

    while (g_running)
    {
        SOCKET client = accept(g_listen_socket, NULL, NULL);

        if (client == INVALID_SOCKET)
        {
            if (!g_running)
                break;

            continue;
        }

        handle_client(client);
    }

    return 0;
}

int remoteview_start(void)
{
    AppSettings s;
    settings_get(&s);

    if (!s.remote_view_enabled)
        return 1; /* Tắt theo cấu hình - không phải lỗi */

    WSADATA wsaData;

    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        return 0;

    g_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    if (g_listen_socket == INVALID_SOCKET)
    {
        wprintf(L"[REMOTEVIEW][%hs] Khong tao duoc socket\n", ERR_REMOTEVIEW_SOCKET_FAIL);
        WSACleanup();
        return 0;
    }

    /* Cho phép bind lại nhanh sau khi restart (SO_REUSEADDR) */
    BOOL reuse = TRUE;
    setsockopt(g_listen_socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port        = htons((u_short)s.remote_view_port);

    if (bind(g_listen_socket, (struct sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR)
    {
        wprintf(L"[REMOTEVIEW][%hs] Bind cong %d that bai\n", ERR_REMOTEVIEW_BIND_FAIL, s.remote_view_port);
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }

    if (listen(g_listen_socket, 8) == SOCKET_ERROR)
    {
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
        WSACleanup();
        return 0;
    }

    g_running = 1;
    g_thread = CreateThread(NULL, 0, accept_loop, NULL, 0, NULL);

    wprintf(L"[REMOTEVIEW] Dang lang nghe tren cong %d (chi doc, can token)\n", s.remote_view_port);

    return 1;
}

void remoteview_stop(void)
{
    if (!g_running)
        return;

    g_running = 0;

    if (g_listen_socket != INVALID_SOCKET)
    {
        /*
         * Đóng socket đang lắng nghe để accept() ở
         * accept_loop() thoát khỏi trạng thái block.
         */
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
    }

    if (g_thread)
    {
        WaitForSingleObject(g_thread, 2000);
        CloseHandle(g_thread);
        g_thread = NULL;
    }

    WSACleanup();

    wprintf(L"[REMOTEVIEW] Da dung\n");
}

int remoteview_restart(void)
{
    remoteview_stop();
    return remoteview_start();
}
