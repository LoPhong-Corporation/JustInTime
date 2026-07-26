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
    void onOpenGoDashboard();
    void onAbout();
    void onCheckForUpdates();
    void onUpdateAvailable(const QString &version, const QString &downloadUrl, const QString &notes);
    void onUpdateUpToDate();
    void onUpdateCheckFailed(const QString &reason);
    void onTrayMessageClicked();
    void onExit();
    //void onStartWebSVAction();
    void onActivated(QSystemTrayIcon::ActivationReason reason);

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
    QAction *m_goDashboardAction     = nullptr;

    QAction *m_aboutAction         = nullptr;
    QAction *m_checkUpdateAction   = nullptr;
    QAction *m_downloadUpdateAction = nullptr;

    UpdateChecker *m_updateChecker = nullptr;
    QTimer         m_updateTimer;
    QString        m_pendingDownloadUrl;
    QString        m_lastNotifiedVersion;
    bool           m_manualUpdateCheck = false;

    std::atomic<bool> m_paused{false};
    bool m_debugVisible = false;
};
