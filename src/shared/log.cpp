//
// log.cpp - xem log.h
//

#include "log.h"

#include <windows.h>

#include <atomic>
#include <clocale>
#include <cstdio>

#include <fcntl.h>
#include <io.h>

namespace {

std::atomic<bool> g_enabled{false};
bool g_consoleCreated = false; // chỉ đụng tới từ GUI thread

bool createConsole()
{
    if (g_consoleCreated)
        return true;

    if (!AllocConsole())
        return false;

    FILE* dummy = nullptr;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);

    _setmode(_fileno(stdout), _O_U16TEXT);
    setlocale(LC_ALL, "");

    /*
     * Bấm [X] hoặc Ctrl+C/Ctrl+Break trên console sẽ kết thúc CẢ tiến
     * trình chứ không chỉ đóng cửa sổ console - với 1 app chạy nền ở
     * tray thì đây là cách rất dễ vô tình tắt agent. Vô hiệu hoá cả hai;
     * muốn ẩn thì bỏ tick "Debug console" trong tray.
     */
    if (HWND wnd = GetConsoleWindow())
    {
        if (HMENU menu = GetSystemMenu(wnd, FALSE))
            DeleteMenu(menu, SC_CLOSE, MF_BYCOMMAND);
    }
    SetConsoleCtrlHandler(nullptr, TRUE);

    g_consoleCreated = true;
    return true;
}

} // namespace

extern "C" int jit_log_enabled(void)
{
    return g_enabled.load(std::memory_order_relaxed) ? 1 : 0;
}

extern "C" void jit_console_show(int visible)
{
    if (visible)
    {
        if (!createConsole())
            return;

        if (HWND wnd = GetConsoleWindow())
            ShowWindow(wnd, SW_SHOW);

        g_enabled.store(true, std::memory_order_relaxed);
    }
    else
    {
        g_enabled.store(false, std::memory_order_relaxed);

        if (HWND wnd = GetConsoleWindow())
            ShowWindow(wnd, SW_HIDE);
    }
}
