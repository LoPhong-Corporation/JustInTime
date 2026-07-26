// main.cpp
//
// Điểm vào app: khởi tạo Qt, tray icon, và một worker thread
// chạy các hàm lõi C (monitor_activity/sync/backup/summary)
// y hệt logic của bản Win32 thuần trước đây - chỉ có lớp UI
// (tray, dialog) là được viết lại bằng Qt/C++.

#include <QApplication>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QMetaObject>

#include <windows.h>
#include <wtsapi32.h>

#pragma comment(lib, "Wtsapi32.lib")

#include <cstdio>
#include <ctime>
#include <atomic>
#include <thread>

#include <io.h>
#include <fcntl.h>
#include <clocale>

extern "C" {
#include "activity.h"
#include "database.h"
#include "sync.h"
#include "backup.h"
#include "config.h"
#include "settings.h"
#include "auth.h"
#include "error_codes.h"
#include "remoteview.h"
}

#include "trayicon.h"

static std::atomic<bool> g_workerRunning{true};

/*
 * Cửa sổ ẩn (message-only, không hiện lên đâu cả, người dùng
 * không bao giờ thấy) chỉ để nhận 2 loại thông báo hệ thống
 * của Windows:
 *
 *   - WM_WTSSESSION_CHANGE (WTS_SESSION_LOCK/UNLOCK): máy khoá/
 *     mở khoá màn hình. Cần gọi WTSRegisterSessionNotification()
 *     lên 1 HWND cụ thể mới nhận được, không phải mọi window
 *     đều tự động có.
 *
 *   - WM_POWERBROADCAST (PBT_APMSUSPEND/PBT_APMRESUMEAUTOMATIC/
 *     PBT_APMRESUMESUSPEND): máy chuẩn bị ngủ (sleep/hibernate)
 *     hoặc vừa thức dậy. Windows tự gửi message này cho MỌI
 *     top-level window, không cần đăng ký riêng.
 *
 * Lý do dùng 1 window Win32 thuần (không phải QWidget) là để
 * chắc chắn nhận được message ngay cả khi app không có bất kỳ
 * QWidget nào đang hiện (đây là app chỉ chạy ở tray) - Qt vẫn
 * bơm message cho window này bình thường vì nó dùng chung
 * message loop chuẩn của Windows (GetMessage/DispatchMessage),
 * không quan trọng window đó do Qt hay do ta tự tạo.
 */
static const wchar_t* kPowerEventClassName = L"JustInTimePowerEventWindowClass";
static HWND g_powerEventWnd = nullptr;

static LRESULT CALLBACK PowerEventWndProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_WTSSESSION_CHANGE:
            if (wParam == WTS_SESSION_LOCK)
            {
                activity_suspend();
            }
            else if (wParam == WTS_SESSION_UNLOCK)
            {
                activity_resume();
            }
            return 0;

        case WM_POWERBROADCAST:
            if (wParam == PBT_APMSUSPEND)
            {
                activity_suspend();
            }
            else if (
                wParam == PBT_APMRESUMEAUTOMATIC ||
                wParam == PBT_APMRESUMESUSPEND
            )
            {
                activity_resume();
            }
            /*
             * Theo MSDN: app nên trả về TRUE cho hầu hết các
             * trường hợp WM_POWERBROADCAST, trừ khi chủ động
             * từ chối 1 yêu cầu PBT_APMQUERYSUSPEND (ta không
             * xử lý loại đó nên luôn trả TRUE).
             */
            return TRUE;

        default:
            return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

/*
 * Tạo cửa sổ ẩn + đăng ký nhận thông báo khoá màn hình. Gọi
 * 1 lần lúc khởi động app, trước khi vào vòng lặp sự kiện
 * chính (app.exec()).
 */
static HWND createPowerEventWindow(void)
{
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = PowerEventWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = kPowerEventClassName;

    RegisterClassExW(&wc);

    HWND hwnd = CreateWindowExW(
        0,
        kPowerEventClassName,
        L"",
        0,
        0, 0, 0, 0,
        HWND_MESSAGE,
        nullptr,
        wc.hInstance,
        nullptr
    );

    if (!hwnd)
    {
        wprintf(
            L"[POWER] Khong tao duoc cua so nhan su kien khoa may/ngu (%lu) - "
            L"tinh nang phat hien khoa man hinh/sleep se khong hoat dong.\n",
            GetLastError()
        );
        return nullptr;
    }

    if (!WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION))
    {
        wprintf(
            L"[POWER] WTSRegisterSessionNotification that bai (%lu) - "
            L"van nhan duoc su kien sleep/resume, nhung khong nhan duoc "
            L"su kien khoa/mo khoa man hinh.\n",
            GetLastError()
        );
    }

    return hwnd;
}

static void destroyPowerEventWindow(HWND hwnd)
{
    if (!hwnd)
        return;

    WTSUnRegisterSessionNotification(hwnd);
    DestroyWindow(hwnd);
    UnregisterClassW(kPowerEventClassName, GetModuleHandleW(nullptr));
}

/*
 * Worker thread: chạy vòng lặp theo dõi/sync/backup/báo cáo.
 * Không đụng tới bất kỳ đối tượng Qt/QWidget nào (chỉ gọi
 * hàm C thuần), nên an toàn khi chạy trên thread riêng,
 * tách biệt khỏi GUI thread để tray/dialog không bị đứng
 * hình mỗi khi có lệnh gọi mạng (WinHTTP là đồng bộ/blocking).
 */
static void workerLoop(TrayIcon *tray)
{
    time_t lastSync       = time(nullptr);
    time_t lastBackup     = time(nullptr);
    time_t lastSummary    = time(nullptr);
    time_t lastLimitCheck = time(nullptr);

    backup_create_snapshot();

    while (g_workerRunning.load())
    {
        AppSettings s;
        settings_get(&s);

        if (!tray->isPaused())
        {
            monitor_activity();
        }

        time_t now = time(nullptr);

        if (now - lastSync >= s.sync_interval_sec)
        {
            sync_pending_records();
            lastSync = now;
        }

        if (now - lastBackup >= s.backup_interval_sec)
        {
            backup_create_snapshot();
            db_delete_old_records(RETENTION_DAYS);
            lastBackup = now;
        }

        if (now - lastSummary >= s.summary_interval_sec)
        {
            db_print_daily_summary();
            lastSummary = now;
        }

        /*
         * Kiểm tra giới hạn app do phụ huynh đặt (chỉ có tác
         * dụng nếu máy này là APP_ROLE_CHILD và đã đăng nhập -
         * activity_check_limits() tự bỏ qua nếu không đúng
         * điều kiện). Không cần nhanh như monitor_activity(),
         * 20s/lần là đủ - tránh gọi mạng liên tục.
         */
        if (now - lastLimitCheck >= 20)
        {
            ActivityLimitEvent events[8];
            int event_count = activity_check_limits(events, 8);

            for (int i = 0; i < event_count; i++)
            {
                QString processName = QString::fromWCharArray(events[i].process_name);
                int reason = events[i].reason;

                /*
                 * QSystemTrayIcon không thread-safe - phải
                 * chuyển lời gọi sang GUI thread bằng
                 * QueuedConnection, không được gọi trực tiếp
                 * từ worker thread này.
                 */
                QMetaObject::invokeMethod(
                    tray,
                    "notifyLimitBlocked",
                    Qt::QueuedConnection,
                    Q_ARG(QString, processName),
                    Q_ARG(int, reason)
                );
            }

            lastLimitCheck = now;
        }

        Sleep(1000);
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    /*
     * Không thoát app khi đóng dialog cuối cùng (Settings,
     * Login...) - app chỉ thoát khi bấm "Exit" trong tray.
     */
    app.setQuitOnLastWindowClosed(false);

    /*
     * Đăng ký nhận sự kiện khoá màn hình / sleep-resume của
     * Windows càng sớm càng tốt, để không bỏ lỡ sự kiện nào
     * xảy ra trong lúc app đang khởi động.
     */
    g_powerEventWnd = createPowerEventWindow();

    /*
     * Console debug: tạo sẵn nhưng ẩn mặc định, để mọi
     * wprintf() trong code C lõi vẫn hoạt động bình thường,
     * chỉ là ẩn khỏi mắt người dùng trừ khi bật trong menu.
     */
    AllocConsole();

    FILE *dummy;
    freopen_s(&dummy, "CONOUT$", "w", stdout);
    freopen_s(&dummy, "CONOUT$", "w", stderr);

    _setmode(_fileno(stdout), _O_U16TEXT);
    setlocale(LC_ALL, "");

    HWND consoleWnd = GetConsoleWindow();

    if (consoleWnd)
        ShowWindow(consoleWnd, SW_HIDE);

    wprintf(L"JustInTime Agent Started (Qt UI)\n");

    if (!QSystemTrayIcon::isSystemTrayAvailable())
    {
        QMessageBox::critical(
            nullptr,
            "JustInTime",
            QString("[%1] System tray is not available on this system.")
                .arg(ERR_UI_TRAY_INIT_FAIL)
        );

        return 1;
    }

    if (!db_init())
    {
        QMessageBox::critical(
            nullptr,
            "JustInTime",
            QString("[%1] Failed to initialize database.").arg(ERR_DB_OPEN_FAIL)
        );

        return 1;
    }

    AppSettings settings;
    settings_load(&settings);

    auth_load_session();

    remoteview_start();

    TrayIcon tray;
    tray.show();

    if (!auth_is_logged_in())
    {
        QMessageBox::information(
            nullptr,
            "JustInTime",
            "Welcome to JustInTime!\n\n"
            "Your activity is still being tracked and saved locally.\n"
            "Log in via the system tray icon's right-click menu\n"
            "to sync your data to the cloud."
        );
    }

    std::thread worker(workerLoop, &tray);

    int ret = app.exec();

    destroyPowerEventWindow(g_powerEventWnd);
    g_powerEventWnd = nullptr;

    /*
     * Thoát: dừng worker thread trước, join xong mới đụng
     * tới database từ main thread để tránh tranh chấp.
     */
    g_workerRunning = false;

    if (worker.joinable())
        worker.join();

    remoteview_stop();

    sync_pending_records();
    backup_create_snapshot();
    db_print_daily_summary();
    db_close();

    return ret;
}
