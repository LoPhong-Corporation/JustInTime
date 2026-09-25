// main.cpp
//
// Điểm vào app: khởi tạo Qt, tray icon, và các worker thread chạy các hàm
// lõi (monitor_activity/sync/backup/summary).
//
// Đợt tối ưu:
//   - TÁCH LÀM 2 LUỒNG thay vì 1:
//       * "watch" : chỉ monitor_activity() + activity_check_limits(),
//         mỗi 1 giây - không bao giờ chờ mạng.
//       * "cloud" : sync_pending_records()/machines_push_heartbeat()/
//         applimits_refresh_my_limits()/backup - có gọi mạng (WinHTTP có
//         thể chặn hàng chục giây nếu mạng chập chờn).
//     Trước đây cả 2 việc này chạy tuần tự trên CÙNG 1 luồng: 1 lần gọi
//     mạng bị treo (proxy lỗi, DNS treo...) làm việc theo dõi hoạt động -
//     và luôn cả activity_check_limits() (chặn app theo giới hạn phụ
//     huynh đặt) - dừng lại theo, có thể tới hàng chục giây.
//   - Console debug được tạo LƯỜI (chỉ khi bật trong tray), xem log.h -
//     trước đây AllocConsole() luôn chạy lúc khởi động (kể cả khi ẩn ngay
//     sau đó), nghĩa là mọi JIT_LOG() trong code lõi luôn tốn công định
//     dạng chuỗi dù chẳng ai xem.
//   - sync_pending_records() trả về != 0 khi lượt sync bị dừng sớm vì mất
//     mạng: luồng cloud giãn nhịp (backoff) thay vì cứ đúng 30 giây lại
//     thử, để không gõ cửa 1 server đang lỗi liên tục.
//

#include <QApplication>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QMetaObject>

#include <windows.h>
#include <wtsapi32.h>

#pragma comment(lib, "Wtsapi32.lib")

#include <cstdio>
#include <ctime>
#include <algorithm>
#include <atomic>
#include <thread>

extern "C" {
#include "activity.h"
#include "database.h"
#include "sync.h"
#include "machines.h"
#include "backup.h"
#include "applimits.h"
#include "config.h"
#include "settings.h"
#include "auth.h"
#include "error_codes.h"
#include "remoteview.h"
}

#include "log.h"
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
        JIT_LOG(
            L"[POWER] Khong tao duoc cua so nhan su kien khoa may/ngu (%lu) - "
            L"tinh nang phat hien khoa man hinh/sleep se khong hoat dong.\n",
            GetLastError()
        );
        return nullptr;
    }

    if (!WTSRegisterSessionNotification(hwnd, NOTIFY_FOR_THIS_SESSION))
    {
        JIT_LOG(
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
 * Luồng "watch": CHỈ theo dõi hoạt động + tự thực thi giới hạn app
 * (activity_check_limits() chỉ đọc bộ nhớ đệm cục bộ - xem applimits.h,
 * không gọi mạng). Không đụng Qt/QWidget, không bao giờ gọi WinHTTP -
 * luôn phản hồi trong ~1 giây kể cả khi mạng đang có vấn đề.
 */
static void watchLoop(TrayIcon *tray)
{
    time_t lastLimitCheck = time(nullptr);

    while (g_workerRunning.load())
    {
        if (!tray->isPaused())
            monitor_activity();

        const time_t now = time(nullptr);

        if (now - lastLimitCheck >= 20)
        {
            ActivityLimitEvent events[8];
            const int event_count = activity_check_limits(events, 8);

            for (int i = 0; i < event_count; i++)
            {
                const QString processName = QString::fromWCharArray(events[i].process_name);
                const int reason = events[i].reason;

                /*
                 * QSystemTrayIcon không thread-safe - phải chuyển lời gọi
                 * sang GUI thread bằng QueuedConnection.
                 */
                QMetaObject::invokeMethod(
                    tray, "notifyLimitBlocked", Qt::QueuedConnection,
                    Q_ARG(QString, processName), Q_ARG(int, reason)
                );
            }

            lastLimitCheck = now;
        }

        Sleep(1000);
    }
}

/*
 * Luồng "cloud": mọi việc có gọi mạng - sync, heartbeat, làm mới bộ nhớ
 * đệm giới hạn app, backup + dọn dữ liệu cũ. Tách khỏi luồng theo dõi để
 * WinHTTP bị treo không ảnh hưởng tới việc theo dõi/chặn app thời gian
 * thực.
 */
static void cloudLoop()
{
    time_t lastSync       = 0;
    time_t lastBackup     = time(nullptr);
    time_t lastSummary    = time(nullptr);
    time_t lastLimitsPull = 0;

    // backoff riêng cho sync khi server/mạng đang lỗi, để không cứ 30s lại
    // gõ cửa 1 nơi chắc chắn đang thất bại.
    int syncBackoffSec = 0;

    backup_create_snapshot();

    while (g_workerRunning.load())
    {
        AppSettings s;
        settings_get(&s);

        const time_t now = time(nullptr);
        const int syncInterval = s.sync_interval_sec + syncBackoffSec;

        if (now - lastSync >= syncInterval)
        {
            const int result = sync_pending_records();
            syncBackoffSec = (result != 0) ? (std::min)(syncBackoffSec == 0 ? 30 : syncBackoffSec * 2, 600) : 0;

            /*
             * FIX (đồng bộ máy không hoạt động): trước đây CHỈ dashboard-go
             * đẩy heartbeat, nên "last_seen" của máy này đứng yên mãi nếu
             * không ai mở dashboard web. Agent tự đẩy heartbeat cùng nhịp
             * với sync - bỏ qua lỗi nếu chưa đăng nhập/mất mạng.
             */
            machines_push_heartbeat();

            lastSync = now;
        }

        // Làm mới bộ nhớ đệm giới hạn app mỗi ~60s (đủ mới, không tốn quá
        // nhiều request) - luồng "watch" chỉ đọc bộ nhớ đệm này.
        if (now - lastLimitsPull >= 60)
        {
            applimits_refresh_my_limits();
            lastLimitsPull = now;
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

        Sleep(1000);
    }
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    /*
     * Không thoát app khi đóng dialog cuối cùng (Settings, Login...) -
     * app chỉ thoát khi bấm "Exit" trong tray.
     */
    app.setQuitOnLastWindowClosed(false);

    /*
     * Đăng ký nhận sự kiện khoá màn hình / sleep-resume của Windows càng
     * sớm càng tốt, để không bỏ lỡ sự kiện nào xảy ra trong lúc app đang
     * khởi động.
     */
    g_powerEventWnd = createPowerEventWindow();

    JIT_LOG(L"JustInTime Agent Started (Qt UI)\n");

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

    std::thread watchThread(watchLoop, &tray);
    std::thread cloudThread(cloudLoop);

    int ret = app.exec();

    destroyPowerEventWindow(g_powerEventWnd);
    g_powerEventWnd = nullptr;

    /*
     * Thoát: dừng cả 2 luồng nền trước, join xong mới đụng tới
     * database/mạng từ main thread để tránh tranh chấp.
     */
    g_workerRunning = false;

    if (watchThread.joinable())
        watchThread.join();
    if (cloudThread.joinable())
        cloudThread.join();

    remoteview_stop();

    sync_pending_records();
    backup_create_snapshot();
    db_print_daily_summary();
    db_close();

    return ret;
}
