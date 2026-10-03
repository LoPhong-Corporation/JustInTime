// trayicon.h
// Icon khay hệ thống bằng Qt (QSystemTrayIcon), gọi thẳng
// vào các hàm C lõi (auth_*, database_*, settings_*).
#pragma once

#include <QObject>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QAction>
#include <QTimer>

#include <atomic>

class UpdateChecker;
class ControlPanelWindow;

namespace jit::dash::ui { class MainWindow; }

class TrayIcon : public QObject
{
    Q_OBJECT

public:
    explicit TrayIcon(QObject *parent = nullptr);

    void show();

    /*
     * Có đang tạm dừng theo dõi không. Được gọi từ worker
     * thread (std::atomic nên an toàn khi đọc cross-thread).
     */
    bool isPaused() const { return m_paused.load(); }

private slots:
    void onLogin();
    void onLogout();
    void onTogglePause();
    void onViewReport();
    void onSettings();
    void onSupabaseSetup();
    void onRemoteView();
    void onToggleDebugConsole();
    void onOpenPythonDashboard();
    void onOpenDashboard();
    void onParentLinkSettings();
    void onParentDashboard();
    void onAbout();
    void onCheckForUpdates();
    void onUpdateAvailable(const QString &version, const QString &downloadUrl, const QString &notes);
    void onUpdateUpToDate();
    void onUpdateCheckFailed(const QString &reason);
    void onTrayMessageClicked();
    void onExit();
    //void onStartWebSVAction();
    void onActivated(QSystemTrayIcon::ActivationReason reason);

    /*
     * Gọi từ worker thread (qua QMetaObject::invokeMethod với
     * Qt::QueuedConnection, KHÔNG được gọi trực tiếp - QSystemTrayIcon
     * không thread-safe) mỗi khi 1 app bị chặn theo giới hạn phụ
     * huynh đặt (xem activity_check_limits() trong core/activity.c).
     * Luôn hiện thông báo rõ ràng, không bao giờ âm thầm.
     */
    void notifyLimitBlocked(const QString &processName, int reason);

private:
    void rebuildMenu();
    void updateTooltip();

    /*
     * Mở (hoặc chỉ mở trình duyệt tới, nếu đã đang chạy sẵn)
     * một trong hai dashboard. Dùng chung logic tìm thư mục/
     * kiểm tra cổng cho cả hai, chỉ khác lệnh khởi chạy.
     */
    void launchDashboard(
        const QString &label,
        quint16 port,
        const QString &program,
        const QStringList &arguments,
        const QString &workingDir
    );

    /* Dựng (lần đầu) hoặc chỉ show()/raise() (các lần sau) m_dashboardWindow. */
    void openDashboardWindow();

    QSystemTrayIcon m_trayIcon;
    QMenu           m_menu;

    QAction *m_accountAction  = nullptr;
    QAction *m_loginAction    = nullptr;
    QAction *m_logoutAction   = nullptr;
    QAction *m_pauseAction    = nullptr;
    QAction *m_reportAction   = nullptr;
    QAction *m_settingsAction = nullptr;
    QAction *m_supabaseAction = nullptr;
    QAction *m_remoteViewAction = nullptr;
    QAction *m_debugAction    = nullptr;
    QAction *m_exitAction     = nullptr;
    QAction *m_startWebSVAction = nullptr;

    QMenu   *m_dashboardMenu       = nullptr;
    QAction *m_pythonDashboardAction = nullptr;
    QAction *m_dashboardAction       = nullptr;

    /*
     * Cửa sổ Dashboard (C++/Qt, xem include/dashboard/ui/mainwindow.h) -
     * TRONG CÙNG tiến trình với agent, không phải spawn dashboard.exe
     * riêng như trước (dashboard-go) nữa. Dựng LƯỜI (chỉ khi bấm "Open
     * Dashboard" lần đầu) rồi giữ lại - các lần bấm sau chỉ show()/raise()
     * lại đúng 1 cửa sổ, không tạo mới.
     */
    jit::dash::ui::MainWindow *m_dashboardWindow = nullptr;

    QAction *m_parentLinkAction      = nullptr; /* "Được giám sát bởi..." - chỉ hiện khi role = CHILD */
    QAction *m_parentDashboardAction = nullptr; /* "Parent Dashboard..." - chỉ hiện khi role = PARENT */

    QAction *m_aboutAction         = nullptr;
    QAction *m_checkUpdateAction   = nullptr;
    QAction *m_downloadUpdateAction = nullptr;

    UpdateChecker *m_updateChecker = nullptr;
    QTimer         m_updateTimer;
    QString        m_pendingDownloadUrl;
    QString        m_lastNotifiedVersion;
    bool           m_manualUpdateCheck = false;

    /*
     * Cửa sổ điều khiển trung tâm (thay cho các QDialog rời rạc
     * trước đây - Settings/Login/Remote View/Monitored By/Family/
     * Supabase Setup giờ đều là các trang bên trong cửa sổ này).
     * Tạo 1 lần duy nhất, dùng lại (openTo()) cho mọi lần mở.
     */
    ControlPanelWindow *m_controlPanel = nullptr;

    std::atomic<bool> m_paused{false};
    bool m_debugVisible = false;
};
