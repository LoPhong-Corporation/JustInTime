//
// mainwindow.cpp
//
// Layout 5 vùng chuẩn (rail / sidebar / tabs+main / right panel / bottom
// bar), theo đúng wireframe người dùng cung cấp:
//
//   ┌───┬──────────┬──────────────────────────────┬──────────┐
//   │Home│ [filter] │ Overview │ CPU │ RAM │ ...     │ Alerts   │
//   │Act.│ ○ MachA  │ ──────────────────────────────│ ...      │
//   │Dev.│ ○ MachB  │   (nội dung tab đang chọn)     │ Snapshot │
//   │Fam.│ ○ MachC  │                                │ [box]    │
//   │   │          │                                │ Procs    │
//   │ ⚙ │          │                                │ ...      │
//   │Acct│         │                                │          │
//   ├───┴──────────┴──────────────────────────────┴──────────┤
//   │ bottom bar (QStatusBar - kế thừa từ QMainWindow)         │
//   └───────────────────────────────────────────────────────┘
//
// 5 icon rail ánh xạ 9 trang cũ thành 4 khu vực nội dung (mỗi khu vực = 1
// hoặc nhiều tab trong centerTabs_) + 2 nút không phải khu vực nội dung
// (Settings mở dialog, Account bật/tắt đăng nhập):
//   Home     -> Overview, CPU, RAM, Disk, Network  (giám sát hệ thống)
//   Activity -> Local Activity, Cloud Data          (tra cứu lịch sử)
//   Devices  -> Devices                             (nhắn tin thiết bị)
//   Family   -> Family                               (quản lý gia đình)
//
// Rail uses native Qt standard icons to avoid font-dependent emoji glyphs.
//
#include "ui/mainwindow.h"
#include "ui/asyncutil.h"
#include "ui/leftrail.h"
#include "ui/machinessidebar.h"
#include "ui/rightpanel.h"
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
#include <QMessageBox>
#include <QStatusBar>
#include <QStyle>
#include <QTabWidget>
#include <QTextStream>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace jit::dash::ui {

MainWindow::MainWindow(Config cfg, std::unique_ptr<LocalDB> db)
    : cfg_(std::move(cfg)), db_(std::move(db))
{
    setWindowTitle("JustInTime - Thời Gian Máy");
    resize(1280, 800);
    buildUi();

    liveTimer_ = new QTimer(this);
    connect(liveTimer_, &QTimer::timeout, this, [this] {
        overviewPage_->tick();
        cpuPage_->tick();
        ramPage_->tick();
        diskPage_->tick();
        networkPage_->tick();
        rightPanel_->tick(); // luôn chạy, bất kể đang xem khu vực nào
    });
    liveTimer_->start(1000);

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
        rightPanel_->refreshStaticInfoOnce();
        rightPanel_->refreshProcesses();
        sidebar_->refresh();
        updateAccountGlyph();
        pushHeartbeat();
    });

    // Top Processes (right panel) đổi chậm hơn CPU/RAM live - làm mới mỗi
    // 5 giây là đủ mượt mà không tốn CreateToolhelp32Snapshot() mỗi giây.
    auto* procTimer = new QTimer(this);
    connect(procTimer, &QTimer::timeout, this, [this] { rightPanel_->refreshProcesses(); });
    procTimer->start(5000);
}

void MainWindow::buildUi()
{
    auto* central = new QWidget;
    auto* body = new QHBoxLayout(central);
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    // ---------------------------------------------------------- Left rail --
    rail_ = new LeftRail;
    rail_->addSection(style()->standardIcon(QStyle::SP_DesktopIcon), "Home - system monitoring");
    rail_->addSection(style()->standardIcon(QStyle::SP_FileDialogContentsView), "Activity - local & cloud history");
    rail_->addSection(style()->standardIcon(QStyle::SP_MessageBoxInformation), "Devices - messages between your machines");
    rail_->addSection(style()->standardIcon(QStyle::SP_DirHomeIcon), "Family - parental controls");
    rail_->onSectionSelected = [this](int index) { showSection(index); };
    rail_->onSettingsClicked = [this] { openSettings(); };
    rail_->onAccountClicked = [this] { onAccountClicked(); };
    body->addWidget(rail_);

    // -------------------------------------------------------------- Sidebar --
    sidebar_ = new MachinesSidebar(cfg_);
    sidebar_->onMachineSelected = [this](QString deviceId) {
        rail_->setCurrentIndex(2); // Devices
        showSection(2);
        devicesPage_->selectDeviceById(deviceId);
    };
    body->addWidget(sidebar_);

    // --------------------------------------------------------- Tabs + main --
    centerTabs_ = new QTabWidget;
    centerTabs_->setObjectName("centerTabs");
    centerTabs_->setDocumentMode(true);
    body->addWidget(centerTabs_, 1);

    // ---------------------------------------------------------- Right panel --
    rightPanel_ = new RightPanel(&collector_);
    body->addWidget(rightPanel_);

    OverviewPage::Callbacks cb;
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

    buildSections();

    setCentralWidget(central);
    showSection(0);
    statusBar()->showMessage(QString("Device: %1").arg(QString::fromStdString(cfg_.displayLabel())));
}

void MainWindow::buildSections()
{
    sections_.resize(4);
    sections_[0] = {{overviewPage_, cpuPage_, ramPage_, diskPage_, networkPage_},
                     {"Overview", "CPU", "RAM", "Disk", "Network"}};
    sections_[1] = {{localPage_, cloudPage_}, {"Local Activity", "Cloud Data"}};
    sections_[2] = {{devicesPage_}, {"Devices"}};
    sections_[3] = {{familyPage_}, {"Family"}};
}

void MainWindow::showSection(int index)
{
    if (index < 0 || index >= static_cast<int>(sections_.size()))
        return;

    // QTabWidget::clear() KHÔNG xoá/huỷ các widget trang (chỉ gỡ khỏi tab
    // bar) - an toàn để chuyển 1 trang qua lại giữa các lần showSection()
    // mà không mất trạng thái (vị trí cuộn, dữ liệu đã tải...).
    centerTabs_->clear();

    const Section& s = sections_[static_cast<size_t>(index)];
    for (size_t i = 0; i < s.pages.size(); i++)
        centerTabs_->addTab(s.pages[i], s.labels[i]);

    // Mỗi lần vào 1 khu vực cần dữ liệu mạng, tải lại - khớp hành vi "mỗi
    // lần chuyển tab thì fetch lại" của dashboard.js gốc.
    switch (index)
    {
        case 1: localPage_->refresh(); cloudPage_->refresh(); break;
        case 2: devicesPage_->refresh(); break;
        case 3: familyPage_->refresh(); break;
        default: break;
    }
}

void MainWindow::selectSection(int index)
{
    rail_->setCurrentIndex(index);
    showSection(index);
}

void MainWindow::updateAccountGlyph()
{
    const auto sess = loadDashSession();
    if (sess && !sess->email.empty())
    {
        const QString initial = QString::fromStdString(sess->email).left(1).toUpper();
        rail_->setAccountGlyph(initial, QString::fromStdString(sess->email) + " (click to log out)");
    }
    else
    {
        rail_->setAccountGlyph(QString(), "Not logged in (click to log in)");
    }
}

void MainWindow::onAccountClicked()
{
    if (loadDashSession())
    {
        if (QMessageBox::question(this, "Log Out", "Log out of this account?") != QMessageBox::Yes)
            return;
        clearDashSession();
        updateAccountGlyph();
        cloudPage_->refresh();
        devicesPage_->refresh();
        familyPage_->refresh();
        sidebar_->refresh();
        return;
    }

    LoginDialog dlg(this);
    if (dlg.exec() == QDialog::Accepted)
    {
        const cloud::Session s = dlg.session();
        saveDashSession({s.accessToken, s.refreshToken, s.userId, s.email});
        updateAccountGlyph();
        overviewPage_->refreshAll();
        cloudPage_->refresh();
        devicesPage_->refresh();
        familyPage_->refresh();
        sidebar_->refresh();
        pushHeartbeat();
    }
}

void MainWindow::openSettings()
{
    const bool loggedIn = loadDashSession().has_value();
    SettingsDialog dlg(loggedIn, [this] { updateAccountGlyph(); }, this);
    dlg.exec();
    updateAccountGlyph();
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
    selectSection(1);
    centerTabs_->setCurrentIndex(1); // Cloud Data
    QMessageBox::information(this, "Print Report",
        "Use your browser-free print: right-click the Cloud Data tables, or export CSV and open it in Excel/Sheets to print.\n\n"
        "(A dedicated print-preview dialog was not carried over from report_print.html in this pass.)");
}

} // namespace jit::dash::ui
