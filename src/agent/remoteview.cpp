//
// remoteview.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface
// extern "C" trong remoteview.h), hành vi 100% giống bản C gốc trước khi chuyển đổi (HTTP
// server cục bộ CHỈ ĐỌC, tắt mặc định, bảo vệ bằng token). Đổi cách
// viết:
//   - std::string cho phần dựng JSON response thay vì snprintf vào
//     nhiều buffer trung gian (process_esc/title_esc/body...).
//   - json_escape() dùng chung từ jsonutil.h thay vì viết lại 1 bản
//     "json_escape_simple" riêng trong file này - bản C cũ có 2 hàm
//     escape JSON khác nhau tồn tại song song trong cùng project
//     (1 ở jsonutil.c, 1 ở remoteview.c) dù làm cùng 1 việc; giờ chỉ
//     còn 1 nguồn sự thật.
//   - g_running vẫn dùng biến toàn cục volatile int (không đổi sang
//     std::atomic để giữ ABI/hành vi giống hệt bản C - đây là 1 chỗ
//     CÓ THỂ cải thiện thêm ở đợt sau, ghi chú lại thay vì âm thầm
//     đổi hành vi đồng bộ hoá).
//

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "remoteview.h"
#include "settings.h"
#include "database.h"
#include "activity.h"
#include "jsonutil.h"
#include "error_codes.h"

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>

#pragma comment(lib, "ws2_32.lib")

namespace {

SOCKET g_listen_socket = INVALID_SOCKET;
HANDLE g_thread = NULL;
volatile int g_running = 0;

std::string jsonEscape(const std::string& s)
{
    std::string out(s.size() * 6 + 16, '\0');
    json_escape(s.c_str(), out.data(), out.size());
    out.resize(strlen(out.c_str()));
    return out;
}

void sendResponse(
    SOCKET client,
    int statusCode,
    const char* statusText,
    const std::string& body)
{
    char header[512];
    snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: application/json\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "\r\n",
        statusCode, statusText, static_cast<int>(body.size())
    );

    send(client, header, static_cast<int>(strlen(header)), 0);
    send(client, body.c_str(), static_cast<int>(body.size()), 0);
}

/*
 * Trích query param đơn giản dạng ?token=xxx từ path. Không cần
 * parser HTTP đầy đủ vì server này chỉ phục vụ vài endpoint GET cố
 * định, không nhận input phức tạp.
 */
std::string extractToken(const std::string& path)
{
    const size_t q = path.find("token=");
    if (q == std::string::npos)
        return "";

    size_t start = q + 6;
    size_t end = start;
    while (end < path.size() && path[end] != '&' && path[end] != ' ')
        end++;

    return path.substr(start, end - start);
}

std::string wideToUtf8(const wchar_t* w)
{
    if (!w || !*w)
        return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    std::string result(len > 0 ? len - 1 : 0, '\0');
    if (len > 0)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, result.data(), len, nullptr, nullptr);
    return result;
}

void handleClient(SOCKET client)
{
    char request[4096] = {0};
    int received = recv(client, request, sizeof(request) - 1, 0);

    if (received <= 0)
    {
        closesocket(client);
        return;
    }
    request[received] = '\0';

    char methodBuf[16] = {0};
    char pathBuf[512] = {0};
    sscanf(request, "%15s %511s", methodBuf, pathBuf);

    AppSettings s;
    settings_get(&s);

    const std::string token = extractToken(pathBuf);

    /*
     * Bắt buộc đúng token, và server phải đang thực sự được bật
     * trong settings (double-check, đề phòng settings đổi giữa lúc
     * server đang chạy).
     */
    if (
        strcmp(methodBuf, "GET") != 0 ||
        !s.remote_view_enabled ||
        s.remote_view_token[0] == '\0' ||
        token != s.remote_view_token
    )
    {
        sendResponse(client, 401, "Unauthorized", R"({"error":"Invalid or missing token"})");
        closesocket(client);
        return;
    }

    const std::string path = pathBuf;

    if (path.rfind("/status", 0) == 0)
    {
        wchar_t processW[512] = {0};
        wchar_t titleW[2048] = {0};
        time_t since = 0;

        activity_get_current(processW, 512, titleW, 2048, &since);

        const std::string processEsc = jsonEscape(wideToUtf8(processW));
        const std::string titleEsc = jsonEscape(wideToUtf8(titleW));

        const std::string body =
            "{\"process_name\":\"" + processEsc + "\",\"window_title\":\"" + titleEsc +
            "\",\"since\":" + std::to_string(static_cast<long long>(since)) + "}";

        sendResponse(client, 200, "OK", body);
    }
    else if (path.rfind("/today", 0) == 0)
    {
        wchar_t summaryW[4096] = {0};
        db_build_daily_summary_text(summaryW, 4096);

        const std::string summaryEsc = jsonEscape(wideToUtf8(summaryW));
        const std::string body = "{\"summary\":\"" + summaryEsc + "\"}";

        sendResponse(client, 200, "OK", body);
    }
    else
    {
        sendResponse(client, 404, "Not Found", R"({"error":"Unknown endpoint. Try /status or /today"})");
    }

    closesocket(client);
}

DWORD WINAPI acceptLoop(LPVOID)
{
    while (g_running)
    {
        SOCKET client = accept(g_listen_socket, NULL, NULL);

        if (client == INVALID_SOCKET)
        {
            if (!g_running)
                break;
            continue;
        }

        handleClient(client);
    }

    return 0;
}

} // namespace

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
    setsockopt(g_listen_socket, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<u_short>(s.remote_view_port));

    if (bind(g_listen_socket, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR)
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
    g_thread = CreateThread(NULL, 0, acceptLoop, NULL, 0, NULL);

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
         * Đóng socket đang lắng nghe để accept() ở acceptLoop() thoát
         * khỏi trạng thái block.
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
