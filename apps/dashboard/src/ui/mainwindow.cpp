#include "ui/mainwindow.h"
#include "ui/asyncutil.h"
#include "ui/logindialog.h"
#include "ui/settingsdialog.h"
#include "ui/pages/overviewpage.h"
#include "ui/pages/metricpages.h"
#include "ui/pages/localpage.h"
#include "ui/pages/cloudpage.h"
#include "ui/pages/devicespage.h"
#include "ui/pages/familypage.h"
#include "cloud.h"
#include "dashsession.h"

#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace jit::dash::ui {

MainWindow::MainWindow(Config cfg, std::unique_ptr<LocalDB> db)
    : cfg_(std::move(cfg)), db_(std::move(db))
{
    setWindowTitle("JustInTime Dashboard");
    resize(1180, 780);
    buildUi();

    liveTimer_ = new QTimer(this);
    connect(liveTimer_, &QTimer::timeout, this, [this] {
        overviewPage_->tick();
        cpuPage_->tick();
        ramPage_->tick();
        diskPage_->tick();
        networkPage_->tick();
    });
    liveTimer_->start(1000);

    // Đẩy heartbeat lên cloud mỗi 30s - CHỈ khi đã đăng nhập (bên trong tự
    // kiểm tra) - giữ song song với agent, không thay thế.
    heartbeatTimer_ = new QTimer(this);
    connect(heartbeatTimer_, &QTimer::timeout, this, &MainWindow::pushHeartbeat);
    heartbeatTimer_->start(30000);

    // Mọi tải dữ liệu ban đầu (có gọi mạng qua runAsync) phải hoãn tới sau
    // khi event loop đã chạy - xem asyncutil.h.
    QTimer::singleShot(0, this, [this] {
        overviewPage_->refreshAll();
        cpuPage_->refreshStaticInfo();
        networkPage_->refreshInterfacesOnce();
        localPage_->refresh();
        updateSessionLabel();
        pushHeartbeat();
    });
}

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    auto* rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(0);

    // ------------------------------------------------------------- Top bar --
    auto* topBar = new QWidget;
    topBar->setStyleSheet("background:#0a1420; border-bottom:1px solid #1c3a5e;");
    auto* topLayout = new QHBoxLayout(topBar);
    auto* title = new QLabel("JustInTime");
    title->setStyleSheet("color:#eef4fb; font-weight:700; font-size:14px; padding:4px 8px;");
    topLayout->addWidget(title);
    topLayout->addStretch();

    sessionLabel_ = new QLabel("Not logged in");
    sessionLabel_->setStyleSheet("color:#7e9ac0;");
    topLayout->addWidget(sessionLabel_);

    loginBtn_ = new QPushButton("Log In");
    connect(loginBtn_, &QPushButton::clicked, this, &MainWindow::onLoginLogoutClicked);
    topLayout->addWidget(loginBtn_);

    auto* settingsBtn = new QPushButton("Settings");
    connect(settingsBtn, &QPushButton::clicked, this, &MainWindow::openSettings);
    topLayout->addWidget(settingsBtn);

    rootLayout->addWidget(topBar);

    // --------------------------------------------------------------- Body --
    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    sidebar_ = new QListWidget;
    sidebar_->setObjectName("sidebarList");
    sidebar_->setFixedWidth(180);
    sidebar_->setStyleSheet(
        "QListWidget { background:#0a1420; border:none; border-right:1px solid #1c3a5e; color:#dce8f7; }"
        "QListWidget::item { padding:10px 14px; }"
        "QListWidget::item:selected { background:#132436; color:#eef4fb; border-left:3px solid #5aa9ff; }");
    for (const char* label : {"Overview", "CPU", "RAM", "Disk", "Network", "Local Activity", "Cloud Data", "Devices", "Family"})
        sidebar_->addItem(label);
    connect(sidebar_, &QListWidget::currentRowChanged, this, &MainWindow::onSidebarRowChanged);
    body->addWidget(sidebar_);

    stack_ = new QStackedWidget;
    stack_->setStyleSheet("background:#000000;");

    OverviewPage::Callbacks cb;
    cb.openFamily = [this] { sidebar_->setCurrentRow(8); };
    cb.openDevices = [this] { sidebar_->setCurrentRow(7); };
    cb.openSettings = [this] { openSettings(); };
    cb.exportCsv = [this] { exportCsv(); };
    cb.printReport = [this] { printReport(); };

    overviewPage_ = new OverviewPage(cfg_, db_.get(), &collector_, cb);
    cpuPage_ = new CpuPage(&collector_);
    ramPage_ = new RamPage(&collector_);
    diskPage_ = new DiskPage(&collector_);
    networkPage_ = new NetworkPage(&collector_);
    localPage_ = new LocalPage(db_.get());
    cloudPage_ = new CloudPage;
    devicesPage_ = new DevicesPage(cfg_, &collector_);
    familyPage_ = new FamilyPage;

    for (QWidget* p : {static_cast<QWidget*>(overviewPage_), static_cast<QWidget*>(cpuPage_), static_cast<QWidget*>(ramPage_),
                        static_cast<QWidget*>(diskPage_), static_cast<QWidget*>(networkPage_), static_cast<QWidget*>(localPage_),
                        static_cast<QWidget*>(cloudPage_), static_cast<QWidget*>(devicesPage_), static_cast<QWidget*>(familyPage_)})
        stack_->addWidget(p);

    body->addWidget(stack_, 1);
    rootLayout->addLayout(body, 1);

    setCentralWidget(central);
    sidebar_->setCurrentRow(0);
    statusBar()->showMessage(QString("Device: %1").arg(QString::fromStdString(cfg_.displayLabel())));
}

void MainWindow::selectPage(int row) { sidebar_->setCurrentRow(row); }

void MainWindow::onSidebarRowChanged(int row)
{
    stack_->setCurrentIndex(row);
    // Mỗi lần chuyển sang 1 trang cần dữ liệu mạng, tải lại - khớp hành vi
    // "mỗi lần chuyển tab thì fetch lại" của dashboard.js gốc.
    switch (row)
    {
        case 5: localPage_->refresh(); break;
        case 6: cloudPage_->refresh(); break;
        case 7: devicesPage_->refresh(); break;
        case 8: familyPage_->refresh(); break;
        default: break;
    }
}

void MainWindow::updateSessionLabel()
{
    const auto sess = loadDashSession();
    if (sess)
    {
        sessionLabel_->setText(QString::fromStdString(sess->email));
        loginBtn_->setText("Log Out");
    }
    else
    {
        sessionLabel_->setText("Not logged in");
        loginBtn_->setText("Log In");
    }
}

void MainWindow::onLoginLogoutClicked()
{
    if (loadDashSession())
    {
        clearDashSession();
        updateSessionLabel();
        cloudPage_->refresh();
        devicesPage_->refresh();
        familyPage_->refresh();
        return;
    }

    LoginDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
    {
        const cloud::Session s = dlg.session();
        saveDashSession({s.accessToken, s.refreshToken, s.userId, s.email});
        updateSessionLabel();
        overviewPage_->refreshAll();
        cloudPage_->refresh();
        devicesPage_->refresh();
        familyPage_->refresh();
        pushHeartbeat();
    }
}

void MainWindow::openSettings()
{
    const bool loggedIn = loadDashSession().has_value();
    SettingsDialog dlg(loggedIn, [this] { updateSessionLabel(); }, this);
    dlg.exec();
    updateSessionLabel();
}

void MainWindow::pushHeartbeat()
{
    const auto sess = loadDashSession();
    if (!sess) return;

    const std::string token = sess->accessToken, deviceId = cfg_.deviceId, label = cfg_.displayLabel();
    const LiveStats live = collector_.live();

    runAsyncVoid(this,
        [token, deviceId, label, live]() { cloud::Client(token).pushHeartbeat(deviceId, label, live.cpuPercent, live.ramPercent, live.diskPercent); },
        [](const std::exception*) {});
}

void MainWindow::exportCsv()
{
    const auto sess = loadDashSession();
    if (!sess) { QMessageBox::information(this, "Export CSV", "Log in first to export cloud-synced activity."); return; }

    const QString path = QFileDialog::getSaveFileName(this, "Export CSV", "justintime_export.csv", "CSV files (*.csv)");
    if (path.isEmpty()) return;

    const std::string token = sess->accessToken;
    runAsync(this,
        [token]() { return cloud::Client(token).recentLogs(1000); },
        [this, path](std::vector<cloud::Client::RecentLog>* logs, const std::exception* e) {
            if (e) { QMessageBox::warning(this, "Export failed", QString::fromStdString(e->what())); return; }

            QFile f(path);
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
            { QMessageBox::warning(this, "Export failed", "Could not open file for writing."); return; }

            QTextStream out(&f);
            out << "device_id,process_name,window_title,duration_seconds,start_time,end_time\r\n";
            auto csvField = [](const QString& s) {
                if (!s.contains(',') && !s.contains('"') && !s.contains('\n')) return s;
                QString q = s; q.replace("\"", "\"\""); return "\"" + q + "\"";
            };
            for (const auto& l : *logs)
                out << csvField(QString::fromStdString(l.deviceId)) << ',' << csvField(QString::fromStdString(l.processName)) << ','
                    << csvField(QString::fromStdString(l.windowTitle)) << ',' << l.duration << ',' << l.startTime << ',' << l.endTime << "\r\n";

            QMessageBox::information(this, "Export complete", QString("Exported %1 records.").arg(logs->size()));
        });
}

void MainWindow::printReport()
{
    sidebar_->setCurrentRow(6); // trang Cloud Data đã có sẵn bảng tổng + gần đây;
    QMessageBox::information(this, "Print Report",
        "Use your browser-free print: right-click the Cloud Data tables, or export CSV and open it in Excel/Sheets to print.\n\n"
        "(A dedicated print-preview dialog was not carried over from report_print.html in this pass.)");
}

} // namespace jit::dash::ui
