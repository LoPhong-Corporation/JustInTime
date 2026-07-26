//
// Created by LoPhongCorporation on 6/24/2026.
// Rewritten: chuyen tu console app sang GUI app chay ngam
// trong system tray. Console debug van con nhung an mac
// dinh, bat/tat qua menu tray "Hien cua so debug".
//

#include "../include/activity.h"
#include "../include/database.h"
#include "../include/sync.h"
#include "../include/backup.h"
#include "../include/config.h"
#include "../include/settings.h"
#include "../include/auth.h"
#include "../include/tray.h"
#include "../include/ui_dialog.h"
#include "../include/error_codes.h"

#include <windows.h>

#include <stdio.h>
#include <time.h>

#include <locale.h>
#include <io.h>
#include <fcntl.h>

#define MONITOR_INTERVAL_MS 1000

static volatile int g_worker_running = 1;
static HWND         g_main_hwnd = NULL;

/*
 * Xu ly Ctrl+C / dong cua so console debug / logoff /
 * shutdown: yeu cau cua so chinh (an, chay message-only)
 * dong lai mot cach an toan qua đúng 1 luong xu ly (main
 * thread), thay vi tu lam moi thu ngay trong handler thread
 * (tranh tranh chap voi worker thread dang dung DB).
 */
/*
 * Du da vo hieu hoa nut dong (X) cua console debug (xem
 * WinMain), handler nay van giu lai de xu ly dung cach
 * cho truong hop logoff/shutdown Windows (nguoi dung tat
 * may), dam bao van sync/backup du lieu truoc khi thoat.
 */
static BOOL WINAPI console_handler(DWORD signal)
{
    switch (signal)
    {
        case CTRL_C_EVENT:
        case CTRL_BREAK_EVENT:
        case CTRL_CLOSE_EVENT:
        case CTRL_LOGOFF_EVENT:
        case CTRL_SHUTDOWN_EVENT:
            if (g_main_hwnd)
                PostMessage(g_main_hwnd, WM_CLOSE, 0, 0);

            /*
             * Windows chi cho vai giay truoc khi force-kill
             * voi CLOSE/LOGOFF/SHUTDOWN, cho message loop
             * kip xu ly xong.
             */
            Sleep(3000);

            return TRUE;

        default:
            return FALSE;
    }
}

/*
 * Worker thread: chay vong lap theo doi/sync/backup/bao cao,
 * doc settings moi vong lap de ap dung thay doi tu dialog
 * Cai dat ngay lap tuc ma khong can khoi dong lai app.
 */
static DWORD WINAPI worker_thread(LPVOID param)
{
    (void)param;

    time_t last_sync    = time(NULL);
    time_t last_backup  = time(NULL);
    time_t last_summary = time(NULL);

    backup_create_snapshot();

    while (g_worker_running)
    {
        AppSettings s;
        settings_get(&s);

        if (!tray_is_paused())
        {
            monitor_activity();
        }

        time_t now = time(NULL);

        if (now - last_sync >= s.sync_interval_sec)
        {
            sync_pending_records();
            last_sync = now;
        }

        if (now - last_backup >= s.backup_interval_sec)
        {
            backup_create_snapshot();
            db_delete_old_records(RETENTION_DAYS);
            last_backup = now;
        }

        if (now - last_summary >= s.summary_interval_sec)
        {
            db_print_daily_summary();
            last_summary = now;
        }

        Sleep(MONITOR_INTERVAL_MS);
    }

    return 0;
}

int WINAPI WinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    LPSTR lpCmdLine,
    int nCmdShow)
{
    (void)hPrevInstance;
    (void)lpCmdLine;
    (void)nCmdShow;

    /*
     * Console debug: tao san nhung an mac dinh, de moi
     * ham wprintf() cu van hoat dong binh thuong, chi la
     * nguoi dung khong thay cua so console tru khi bat
     * "Hien cua so debug" trong menu tray.
     */
    AllocConsole();

    FILE* dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);

    _setmode(_fileno(stdout), _O_U16TEXT);
    setlocale(LC_ALL, "");

    HWND console_wnd = GetConsoleWindow();

    if (console_wnd)
    {
        /*
         * Vo hieu hoa nut dong (X) cua cua so console debug.
         * Ly do: mot khi console bi dong (CTRL_CLOSE_EVENT),
         * Windows se luon buoc ket thuc tien trinh sau vai
         * giay du handler co xu ly gi di nua - khong the
         * "huy" viec dong console nhu voi 1 cua so thuong.
         * Vi vay cach dung la khong cho phep dong no, chi an/
         * hien qua menu tray.
         */
        HMENU sys_menu = GetSystemMenu(console_wnd, FALSE);

        if (sys_menu)
        {
            DeleteMenu(sys_menu, SC_CLOSE, MF_BYCOMMAND);
        }

        ShowWindow(console_wnd, SW_HIDE);
    }

    wprintf(L"JustInTime Agent Started (Running in system tray)\n");

    if (!db_init())
    {
        wchar_t msg[128];
        swprintf(msg, 128, L"[%hs] Không thể khởi tạo database.", ERR_DB_OPEN_FAIL);
        show_error_message(msg);
        return 1;
    }

    AppSettings settings;
    settings_load(&settings);

    auth_load_session();

    SetConsoleCtrlHandler(console_handler, TRUE);

    g_main_hwnd = tray_init(hInstance);

    if (!g_main_hwnd)
    {
        wchar_t msg[128];
        swprintf(msg, 128, L"[%hs] Không thể tạo tray icon.", ERR_UI_TRAY_INIT_FAIL);
        show_error_message(msg);
        db_close();
        return 1;
    }

    if (!auth_is_logged_in())
    {
        /*show_info_message(
            L"Chào mừng bạn đến với JustInTime!\n\n"
            L"Dữ liệu vẫn được theo dõi và lưu local bình thường.\n"
            L"Đăng nhập qua menu chuột phải vào icon khay hệ thống\n"
            L"để đồng bộ dữ liệu lên cloud."
        );*/

        //SLIENT MODE
    }

    HANDLE worker = CreateThread(
        NULL, 0,
        worker_thread,
        NULL, 0,
        NULL
    );

    MSG msg;

    while (GetMessage(&msg, NULL, 0, 0))
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    g_worker_running = 0;

    if (worker)
    {
        WaitForSingleObject(worker, 5000);
        CloseHandle(worker);
    }

    sync_pending_records();
    backup_create_snapshot();
    db_print_daily_summary();
    db_close();

    return (int)msg.wParam;
}
