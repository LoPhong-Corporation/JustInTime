// trayicon.cpp

#include "trayicon.h"
#include "logindialog.h"
#include "settingsdialog.h"
#include "supabasesetupdialog.h"
#include "remoteviewdialog.h"
#include "updatechecker.h"
#include "parentlinkdialog.h"
#include "parentdialog.h"

#include <QApplication>
#include <QMessageBox>
#include <QStyle>
#include <QProcess>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QTcpSocket>
#include <QHostAddress>
#include <QTimer>

#include <windows.h>

extern "C" {
#include "auth.h"
#include "database.h"
#include "config.h"
#include "activity.h"
#include "settings.h"
}

namespace {

/*
 * Cả "dashboard" (Python) và "dashboard-go" đều không được CMake copy
 * theo exe (chưa có bước cài đặt chính thức), nên tìm chúng bằng cách
 * dò vài cấp thư mục cha kể từ nơi JustInTime.exe đang chạy — bản dev
 * build thường nằm ở build/<Config>/JustInTime.exe, tức thư mục gốc
 * repo cách đó 2 cấp.
 */
QString findResourceDir(const QString &relativeName)
{
    const QString exeDir = QCoreApplication::applicationDirPath();

    const QStringList candidates = {
        exeDir + "/" + relativeName,
        exeDir + "/../" + relativeName,
        exeDir + "/../../" + relativeName,
        exeDir + "/../../../" + relativeName,
    };

    for (const QString &candidate : candidates)
    {
        QDir dir(candidate);
        if (dir.exists())
            return dir.absolutePath();
    }

    // Không tìm thấy đâu cả — trả về ứng viên "cạnh exe" để thông báo
    // lỗi phía sau còn có đường dẫn cụ thể để hiển thị cho người dùng.
    return QDir(exeDir + "/" + relativeName).absolutePath();
}

bool isPortOpen(quint16 port)
{
    QTcpSocket socket;
    socket.connectToHost(QHostAddress::LocalHost, port);
    return socket.waitForConnected(150);
}

} // namespace

TrayIcon::TrayIcon(QObject *parent) : QObject(parent)
{
    m_trayIcon.setIcon(qApp->style()->standardIcon(QStyle::SP_ComputerIcon));

    m_accountAction = m_menu.addAction(QString());
    m_accountAction->setEnabled(false);

    m_loginAction  = m_menu.addAction("Log in / Sign up...", this, &TrayIcon::onLogin);
    m_logoutAction = m_menu.addAction("Log out", this, &TrayIcon::onLogout);

    m_menu.addSeparator();

    m_pauseAction  = m_menu.addAction("Pause tracking", this, &TrayIcon::onTogglePause);
    m_reportAction = m_menu.addAction("View today's report", this, &TrayIcon::onViewReport);

    m_menu.addSeparator();

    m_settingsAction = m_menu.addAction("Settings...", this, &TrayIcon::onSettings);
    m_supabaseAction = m_menu.addAction("Supabase Setup...", this, &TrayIcon::onSupabaseSetup);
    m_remoteViewAction = m_menu.addAction("Remote View...", this, &TrayIcon::onRemoteView);

    m_menu.addSeparator();

    m_dashboardMenu = m_menu.addMenu("Dashboards");
    m_pythonDashboardAction = m_dashboardMenu->addAction(
        "Open Web Dashboard (Python)", this, &TrayIcon::onOpenPythonDashboard
    );
    m_goDashboardAction = m_dashboardMenu->addAction(
        "Open Local Dashboard (Go)", this, &TrayIcon::onOpenGoDashboard
    );

    const QString dashboardTip =
        "Both dashboards show the same data on http://127.0.0.1:5000 "
        "(they're two implementations of the same dashboard, not meant "
        "to run at the same time). Whichever is already running will "
        "just be opened again.";
    m_pythonDashboardAction->setToolTip(dashboardTip);
    m_goDashboardAction->setToolTip(dashboardTip);

    m_menu.addSeparator();

    m_parentLinkAction = m_menu.addAction(
        "Được giám sát bởi...", this, &TrayIcon::onParentLinkSettings
    );
    m_parentLinkAction->setToolTip(
        "Xem đầy đủ danh sách phụ huynh đang xin/đã được xem hoạt động của máy này, "
        "và thu hồi quyền xem bất kỳ lúc nào."
    );

    m_parentDashboardAction = m_menu.addAction(
        "Parent Dashboard...", this, &TrayIcon::onParentDashboard
    );
    m_parentDashboardAction->setToolTip(
        "Mời con, xem hoạt động, và đặt giới hạn thời gian/chặn app."
    );

    m_debugAction = m_menu.addAction("Show debug console", this, &TrayIcon::onToggleDebugConsole);
    //m_startWebSVAction = m_menu.addAction("Start Web Dashboard", this, &TrayIcon::onStartWebSVAction);
    m_debugAction->setCheckable(true);

    m_menu.addSeparator();

    m_checkUpdateAction = m_menu.addAction("Check for Updates...", this, &TrayIcon::onCheckForUpdates);
    m_aboutAction       = m_menu.addAction("About JustInTime...", this, &TrayIcon::onAbout);

    m_menu.addSeparator();
    m_exitAction = m_menu.addAction("Exit", this, &TrayIcon::onExit);

    m_trayIcon.setContextMenu(&m_menu);

    connect(&m_trayIcon, &QSystemTrayIcon::activated, this, &TrayIcon::onActivated);
    connect(&m_trayIcon, &QSystemTrayIcon::messageClicked, this, &TrayIcon::onTrayMessageClicked);

    /*
     * Kiểm tra cập nhật: 1 lần khi khởi động (chờ 5s cho app ổn
     * định), sau đó định kỳ mỗi 6 tiếng. Đây là kiểm tra NGẦM
     * (m_manualUpdateCheck = false) - chỉ hiện tray notification
     * khi thực sự CÓ bản mới, không làm phiền người dùng bằng
     * thông báo "đã là bản mới nhất" mỗi 6 tiếng.
     */
    m_updateChecker = new UpdateChecker(this);

    connect(m_updateChecker, &UpdateChecker::updateAvailable, this, &TrayIcon::onUpdateAvailable);
    connect(m_updateChecker, &UpdateChecker::upToDate, this, &TrayIcon::onUpdateUpToDate);
    connect(m_updateChecker, &UpdateChecker::checkFailed, this, &TrayIcon::onUpdateCheckFailed);

    m_updateTimer.setInterval(6 * 60 * 60 * 1000); // 6 gio
    connect(&m_updateTimer, &QTimer::timeout, this, [this]() {
        m_manualUpdateCheck = false;
        m_updateChecker->checkNow();
    });
    m_updateTimer.start();

    QTimer::singleShot(5000, this, [this]() {
        m_manualUpdateCheck = false;
        m_updateChecker->checkNow();
    });

    rebuildMenu();
}

void TrayIcon::show()
{
    m_trayIcon.show();
}

void TrayIcon::onActivated(QSystemTrayIcon::ActivationReason reason)
{
    Q_UNUSED(reason);

    /*
     * Đảm bảo trạng thái đăng nhập/tạm dừng luôn cập nhật
     * đúng trước khi hiện menu (vì có thể đổi từ dialog).
     */
    rebuildMenu();
}

void TrayIcon::rebuildMenu()
{
    AuthSession session;
    auth_get_session(&session);

    if (session.logged_in)
    {
        m_accountAction->setText(
            QString("Logged in: %1").arg(QString::fromUtf8(session.email))
        );
        m_accountAction->setVisible(true);
        m_loginAction->setVisible(false);
        m_logoutAction->setVisible(true);
    }
    else
    {
        m_accountAction->setVisible(false);
        m_loginAction->setVisible(true);
        m_logoutAction->setVisible(false);
    }

    m_pauseAction->setText(m_paused.load() ? "Resume tracking" : "Pause tracking");
    m_debugAction->setChecked(m_debugVisible);

    AppSettings s;
    settings_get(&s);

    /*
     * Chỉ hiện đúng 1 trong 2 mục tuỳ vai trò đang chọn trong
     * Settings - không hiện cả 2 để tránh gây nhầm lẫn (máy con
     * không có gì để "Parent Dashboard", máy phụ huynh không có
     * ý nghĩa "Được giám sát bởi...").
     */
    m_parentLinkAction->setVisible(s.app_role == APP_ROLE_CHILD);
    m_parentDashboardAction->setVisible(s.app_role == APP_ROLE_PARENT);

    updateTooltip();
}

void TrayIcon::updateTooltip()
{
    AuthSession session;
    auth_get_session(&session);

    QString tip;

    if (session.logged_in)
    {
        tip = QString("JustInTime - %1%2")
                  .arg(QString::fromUtf8(session.email))
                  .arg(m_paused.load() ? " (paused)" : "");
    }
    else
    {
        tip = QString("JustInTime - not logged in%1")
                  .arg(m_paused.load() ? " (paused)" : "");
    }

    m_trayIcon.setToolTip(tip);
}

void TrayIcon::onLogin()
{
    LoginDialog dlg;

    if (dlg.exec() == QDialog::Accepted)
        rebuildMenu();
}

void TrayIcon::onLogout()
{
    if (
        QMessageBox::question(
            nullptr,
            "JustInTime",
            "Log out of the current account?"
        ) == QMessageBox::Yes
    )
    {
        auth_logout();
        rebuildMenu();

        QMessageBox::information(
            nullptr,
            "JustInTime",
            "Logged out. Data is still saved locally, but cloud sync is paused."
        );
    }
}

void TrayIcon::onTogglePause()
{
    m_paused = !m_paused.load();
    rebuildMenu();
}

void TrayIcon::onViewReport()
{
    wchar_t buffer[4096] = {0};

    db_build_daily_summary_text(buffer, 4096);

    QString text = QString("Today's summary:\n\n%1")
                       .arg(QString::fromWCharArray(buffer));

    QMessageBox::information(nullptr, "JustInTime", text);
}

void TrayIcon::onSettings()
{
    SettingsDialog dlg;
    dlg.exec();

    /*
     * Role (Con/Phụ huynh) hoặc các cấu hình khác có thể đã
     * đổi - làm mới menu ngay, không cần khởi động lại app.
     */
    rebuildMenu();
}

void TrayIcon::onSupabaseSetup()
{
    SupabaseSetupDialog dlg;
    dlg.exec();
}

void TrayIcon::onRemoteView()
{
    RemoteViewDialog dlg;
    dlg.exec();
}

void TrayIcon::onToggleDebugConsole()
{
    m_debugVisible = !m_debugVisible;

    HWND console = GetConsoleWindow();

    if (console)
        ShowWindow(console, m_debugVisible ? SW_SHOW : SW_HIDE);

    m_debugAction->setChecked(m_debugVisible);
}

/*void TrayIcon::onStartWebSVAction()
{
    system("pythonw ./dashboard/app.pyw");
    // Start the WebSV server
    if (system("pythonw ./dashboard/app.pyw") == -1)
    {
        QMessageBox::critical(nullptr, "JustInTime", "Failed to start WebSV server.");
    }
}*/

void TrayIcon::launchDashboard(
    const QString &label,
    quint16 port,
    const QString &program,
    const QStringList &arguments,
    const QString &workingDir
)
{
    if (isPortOpen(port))
    {
        /*
         * Đã có tiến trình lắng nghe cổng này rồi (Python và Go dùng
         * chung cổng 5000 vì cả hai là 2 cách triển khai của CÙNG một
         * dashboard, không nhằm chạy song song) - chỉ cần mở trình
         * duyệt tới, không cần spawn thêm tiến trình mới.
         */
        QDesktopServices::openUrl(QUrl(QString("http://127.0.0.1:%1/").arg(port)));
        return;
    }

    qint64 pid = 0;
    const bool started = QProcess::startDetached(program, arguments, workingDir, &pid);

    if (!started)
    {
        QMessageBox::warning(
            nullptr,
            "JustInTime",
            QString("Could not start %1.\n\nTried to run:\n%2 %3\n\nWorking directory:\n%4")
                .arg(label)
                .arg(program)
                .arg(arguments.join(' '))
                .arg(workingDir)
        );
        return;
    }

    /*
     * Chờ một chút cho server bind cổng + render lần đầu rồi mới mở
     * trình duyệt, thay vì mở ngay lập tức và dễ gặp "connection
     * refused" nếu server chưa kịp sẵn sàng.
     */
    QTimer::singleShot(1200, [port]() {
        QDesktopServices::openUrl(QUrl(QString("http://127.0.0.1:%1/").arg(port)));
    });
}

void TrayIcon::onOpenPythonDashboard()
{
    const QString dashboardDir = findResourceDir("dashboard");
    const QString scriptPath = dashboardDir + "/app.pyw";

    if (!QFile::exists(scriptPath))
    {
        QMessageBox::warning(
            nullptr,
            "JustInTime",
            QString(
                "Could not find the Python dashboard at:\n%1\n\n"
                "Make sure the 'dashboard' folder sits next to JustInTime.exe "
                "(or in the project root, for dev builds), and that Python "
                "with the packages in dashboard/requirements.txt is installed."
            ).arg(scriptPath)
        );
        return;
    }

    launchDashboard("the Python dashboard", 5000, "pythonw", { scriptPath }, dashboardDir);
}

void TrayIcon::onOpenGoDashboard()
{
    const QString goDir = findResourceDir("dashboard-go");
    const QString exePath = goDir + "/dashboard.exe";

    if (!QFile::exists(exePath))
    {
        QMessageBox::warning(
            nullptr,
            "JustInTime",
            QString(
                "Could not find the Go dashboard at:\n%1\n\n"
                "Build it first with:\n"
                "  cd dashboard-go\n"
                "  go build -o dashboard.exe ./cmd/dashboard"
            ).arg(exePath)
        );
        return;
    }

    /*
     * Không truyền --tray ở đây: cái tray bạn đang thấy CHÍNH LÀ
     * tray rồi. dashboard.exe khi chạy không có --tray chỉ start
     * server + tự mở trình duyệt rồi chạy nền, không tạo thêm icon
     * khay hệ thống thứ hai (xem cmd/dashboard/main.go).
     */
    launchDashboard("the Go dashboard", 5000, exePath, {}, goDir);
}

void TrayIcon::onParentLinkSettings()
{
    if (!auth_is_logged_in())
    {
        QMessageBox::information(
            nullptr, "JustInTime",
            "Bạn cần đăng nhập trước để xem/thu hồi liên kết phụ huynh."
        );
        return;
    }

    ParentLinkDialog dlg;
    dlg.exec();
}

void TrayIcon::onParentDashboard()
{
    if (!auth_is_logged_in())
    {
        QMessageBox::information(
            nullptr, "JustInTime",
            "Bạn cần đăng nhập trước để dùng Parent Dashboard."
        );
        return;
    }

    ParentDialog dlg;
    dlg.exec();
}

void TrayIcon::onAbout()
{
    QMessageBox::about(
        nullptr,
        "About JustInTime",
        QString(
            "<h3>JustInTime</h3>"
            "<p>Version %1</p>"
            "<p>Lightweight activity tracker with optional Supabase cloud sync, "
            "a local/offline dashboard, and cross-device messaging.</p>"
            "<p>Publisher: %2<br>"
            "<a href=\"%3\">%3</a></p>"
        ).arg(APP_VERSION, APP_PUBLISHER, APP_WEBSITE)
    );
}

void TrayIcon::onCheckForUpdates()
{
    m_manualUpdateCheck = true;

    m_checkUpdateAction->setEnabled(false);
    m_checkUpdateAction->setText("Checking for updates...");

    m_updateChecker->checkNow();
}

void TrayIcon::onUpdateAvailable(const QString &version, const QString &downloadUrl, const QString &notes)
{
    m_checkUpdateAction->setEnabled(true);
    m_checkUpdateAction->setText("Check for Updates...");

    m_pendingDownloadUrl = downloadUrl;

    if (!m_downloadUpdateAction)
    {
        m_downloadUpdateAction = new QAction(QString(), &m_menu);
        m_menu.insertAction(m_checkUpdateAction, m_downloadUpdateAction);

        connect(m_downloadUpdateAction, &QAction::triggered, this, [this]() {
            if (!m_pendingDownloadUrl.isEmpty())
                QDesktopServices::openUrl(QUrl(m_pendingDownloadUrl));
        });
    }

    m_downloadUpdateAction->setText(QString("\u2B06 Update available: v%1").arg(version));
    m_downloadUpdateAction->setVisible(true);

    /*
     * Chỉ hiện balloon notification 1 lần cho mỗi phiên bản mới
     * (tránh spam mỗi 6 tiếng nếu người dùng chưa cập nhật ngay),
     * trừ khi người dùng tự bấm "Check for Updates..." thì luôn
     * hiện để xác nhận là đã kiểm tra xong.
     */
    if (m_manualUpdateCheck || m_lastNotifiedVersion != version)
    {
        m_lastNotifiedVersion = version;

        QString message = QString("Version %1 is available.").arg(version);

        if (!notes.isEmpty())
            message += QString("\n%1").arg(notes);

        message += QStringLiteral("\n\nClick here to download.");

        m_trayIcon.showMessage(
            "JustInTime Update Available",
            message,
            QSystemTrayIcon::Information,
            8000
        );
    }

    m_manualUpdateCheck = false;
}

void TrayIcon::onUpdateUpToDate()
{
    m_checkUpdateAction->setEnabled(true);
    m_checkUpdateAction->setText("Check for Updates...");

    if (m_manualUpdateCheck)
    {
        QMessageBox::information(
            nullptr,
            "JustInTime",
            QString("You're running the latest version (%1).").arg(APP_VERSION)
        );
    }

    m_manualUpdateCheck = false;
}

void TrayIcon::onUpdateCheckFailed(const QString &reason)
{
    m_checkUpdateAction->setEnabled(true);
    m_checkUpdateAction->setText("Check for Updates...");

    /*
     * Kiểm tra ngầm thất bại (vd không có mạng) thì âm thầm bỏ
     * qua - sẽ tự thử lại ở lần định kỳ tiếp theo. Chỉ báo lỗi
     * ra ngoài khi người dùng chủ động bấm kiểm tra.
     */
    if (m_manualUpdateCheck)
    {
        QMessageBox::warning(
            nullptr,
            "JustInTime",
            QString("Could not check for updates:\n%1").arg(reason)
        );
    }

    m_manualUpdateCheck = false;
}

void TrayIcon::onTrayMessageClicked()
{
    if (!m_pendingDownloadUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(m_pendingDownloadUrl));
}

void TrayIcon::onExit()
{
    qApp->quit();
}

void TrayIcon::notifyLimitBlocked(const QString &processName, int reason)
{
    const QString title = "JustInTime";

    QString body;

    if (reason == LIMIT_REASON_BLOCKED)
    {
        body = QString("\"%1\" has been closed - a parent has blocked this app.").arg(processName);
    }
    else if (reason == LIMIT_REASON_TIME_UP)
    {
        body = QString("\"%1\" has been closed - today's time limit set by a parent has been reached.").arg(processName);
    }
    else
    {
        body = QString("\"%1\" has been closed by a parent-set limit.").arg(processName);
    }

    m_trayIcon.showMessage(title, body, QSystemTrayIcon::Warning, 8000);
}
