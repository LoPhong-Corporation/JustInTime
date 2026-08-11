// controlpanelwindow.cpp

#include "controlpanelwindow.h"

#include <cstring>

#include "loginpage.h"
#include "settingspage.h"
#include "remoteviewpage.h"
#include "parentlinkpage.h"
#include "supabasesetuppage.h"
#include "parentpage.h"

#include <QListWidget>
#include <QListWidgetItem>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QDesktopServices>
#include <QUrl>
#include <QStyle>
#include <QApplication>
#include <QCloseEvent>
#include <QSize>
#include <QFont>
#include <QScrollArea>
#include <QProcess>
#include <QTcpSocket>
#include <QHostAddress>
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QPropertyAnimation>
#include <QMessageBox>

extern "C" {
#include "auth.h"
#include "settings.h"
#include "database.h"
#include "i18n.h"
#include "machines.h"
#include "device.h"
}

#include "config.h"

static QString tr_(const char *key)
{
    return QString::fromUtf8(i18n_t(key));
}

// Icon nhỏ (emoji) theo NHÓM ứng dụng, chọn bằng cách khớp từ khoá
// đơn giản trên tên process - agent C không lưu đường dẫn đầy đủ của
// exe (chỉ tên), nên không thể trích icon THẬT của từng app một
// cách đáng tin cậy; icon theo nhóm vẫn cho cảm giác trực quan, dễ
// quét mắt hơn nhiều so với 1 khối text phẳng như trước đây.
static QString iconForProcess(const QString &processNameRaw)
{
    const QString p = processNameRaw.toLower();

    struct Rule { const char *needle; const char *emoji; };
    static const Rule rules[] = {
        {"chrome",    "🌐"}, {"firefox",  "🌐"}, {"msedge",   "🌐"}, {"opera", "🌐"}, {"brave", "🌐"},
        {"discord",   "💬"}, {"zalo",     "💬"}, {"telegram", "💬"}, {"messenger", "💬"}, {"slack", "💬"},
        {"code",      "🧑‍💻"}, {"devenv",   "🧑‍💻"}, {"pycharm",  "🧑‍💻"}, {"clion", "🧑‍💻"}, {"idea", "🧑‍💻"},
        {"steam",     "🎮"}, {"riot",     "🎮"}, {"valorant", "🎮"}, {"leagueclient", "🎮"}, {"epicgames", "🎮"},
        {"word",      "📄"}, {"excel",    "📊"}, {"powerpnt", "📊"}, {"acrobat", "📄"}, {"onenote", "📝"},
        {"spotify",   "🎵"}, {"vlc",      "🎬"}, {"potplayer","🎬"}, {"itunes", "🎵"},
        {"explorer",  "🗂️"}, {"cmd",      "⌨️"}, {"powershell","⌨️"}, {"terminal", "⌨️"},
        {"photoshop", "🎨"}, {"figma",    "🎨"}, {"illustrator","🎨"},
    };

    for (const auto &rule : rules)
        if (p.contains(QString::fromLatin1(rule.needle)))
            return QString::fromUtf8(rule.emoji);

    return QStringLiteral("🖥️"); // mặc định: app không nhận diện được nhóm
}

// Xoá phần mở rộng ".exe" cho gọn khi hiển thị (agent lưu nguyên tên
// file, dashboard Go cũng hiển thị y hệt vậy nên KHÔNG đổi dữ liệu
// gốc, chỉ bỏ đuôi lúc render).
static QString prettyProcessName(const QString &raw)
{
    QString s = raw;
    if (s.endsWith(".exe", Qt::CaseInsensitive))
        s.chop(4);
    return s;
}

static QString formatHMS(long long totalSeconds)
{
    long long h = totalSeconds / 3600;
    long long m = (totalSeconds % 3600) / 60;
    long long s = totalSeconds % 60;
    return QString("%1:%2:%3")
        .arg(h, 2, 10, QChar('0'))
        .arg(m, 2, 10, QChar('0'))
        .arg(s, 2, 10, QChar('0'));
}

ControlPanelWindow::ControlPanelWindow(QWidget *parent) : QMainWindow(parent)
{
    /*
     * Chỉ gắn thẻ vào tiêu đề cửa sổ khi đang chạy bản EXPERIMENTAL -
     * đây là dấu hiệu CẢNH BÁO cho người dùng/dev biết ngay họ đang
     * chạy bản thử nghiệm, không cần mở tới trang Development mới
     * biết. Bản STABLE (mặc định) không cần thẻ gì thêm.
     */
#ifdef JUSTINTIME_CORE_MODE
    if (QString(JUSTINTIME_CORE_MODE) == "EXPERIMENTAL")
        setWindowTitle(tr_("cp.title") + " [EXPERIMENTAL]");
    else
        setWindowTitle(tr_("cp.title"));
#else
    setWindowTitle(tr_("cp.title"));
#endif
    resize(940, 640);
    setMinimumSize(800, 560);

    // Phông chữ cơ sở cho toàn cửa sổ - trước đây dùng font mặc định
    // của hệ thống ở đủ mọi kích thước lộn xộn, giờ cố định 1 baseline
    // nhất quán (Segoe UI có sẵn trên mọi bản Windows).
    QFont baseFont("Segoe UI", 10);
    setFont(baseFont);

    auto *central = new QWidget(this);
    auto *outerLayout = new QHBoxLayout(central);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    // ---------------- Sidebar ----------------

    auto *sidebarWrap = new QWidget(central);
    sidebarWrap->setObjectName("sidebarWrap");
    sidebarWrap->setFixedWidth(208);
    auto *sidebarWrapLayout = new QVBoxLayout(sidebarWrap);
    sidebarWrapLayout->setContentsMargins(0, 0, 0, 0);
    sidebarWrapLayout->setSpacing(0);

    // Khối "thương hiệu" phía trên sidebar - trước đây sidebar chỉ có
    // 1 danh sách trơ trọi, không có gì cho biết đây là app nào, cảm
    // giác như 1 dialog cấu hình chung chung. Giờ có tên app + icon
    // ngay đầu, giống bố cục app desktop thật (kiểu Settings Windows/
    // Slack) thay vì 1 popup xấu xí.
    auto *brandBar = new QWidget(sidebarWrap);
    brandBar->setObjectName("brandBar");
    auto *brandLayout = new QHBoxLayout(brandBar);
    brandLayout->setContentsMargins(18, 18, 14, 16);
    brandLayout->setSpacing(10);

    auto *brandIcon = new QLabel(brandBar);
    brandIcon->setPixmap(style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(22, 22));

    auto *brandText = new QLabel("JustInTime", brandBar);
    brandText->setObjectName("brandText");

    brandLayout->addWidget(brandIcon);
    brandLayout->addWidget(brandText);
    brandLayout->addStretch();

    m_sidebar = new QListWidget(sidebarWrap);
    m_sidebar->setFrameShape(QFrame::NoFrame);
    m_sidebar->setSpacing(3);
    m_sidebar->setIconSize(QSize(18, 18));

    sidebarWrapLayout->addWidget(brandBar);
    sidebarWrapLayout->addWidget(m_sidebar, 1);

    // ---------------- Trang bên phải ----------------

    m_stack = new QStackedWidget(central);

    auto *stackWrap = new QWidget(central);
    stackWrap->setObjectName("stackWrap");
    auto *stackWrapLayout = new QVBoxLayout(stackWrap);
    stackWrapLayout->setContentsMargins(36, 30, 36, 30);
    stackWrapLayout->addWidget(m_stack);

    outerLayout->addWidget(sidebarWrap);
    outerLayout->addWidget(stackWrap, 1);

    setCentralWidget(central);

    // ---------------- Tạo các trang (thứ tự khớp addItem bên dưới) ----------------

    buildOverviewPage();
    m_stack->addWidget(m_overviewPage);
    m_sidebar->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_DesktopIcon), tr_("cp.nav_overview")));
    m_rowOverview = m_sidebar->count() - 1;

    m_loginPage = new LoginPage(m_stack);
    m_stack->addWidget(m_loginPage);
    m_sidebar->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_DialogYesButton), tr_("cp.nav_account")));
    m_rowAccount = m_sidebar->count() - 1;
    connect(m_loginPage, &LoginPage::loggedIn, this, &ControlPanelWindow::onLoggedInOrOut);
    connect(m_loginPage, &LoginPage::loggedOut, this, &ControlPanelWindow::onLoggedInOrOut);

    m_settingsPage = new SettingsPage(m_stack);
    m_stack->addWidget(m_settingsPage);
    m_sidebar->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_FileDialogDetailedView), tr_("cp.nav_settings")));
    m_rowSettings = m_sidebar->count() - 1;
    connect(m_settingsPage, &SettingsPage::settingsSaved, this, &ControlPanelWindow::onSettingsSaved);

    m_remoteViewPage = new RemoteViewPage(m_stack);
    m_stack->addWidget(m_remoteViewPage);
    m_sidebar->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_DriveNetIcon), tr_("cp.nav_remoteview")));
    m_rowRemoteView = m_sidebar->count() - 1;

    m_parentLinkPage = new ParentLinkPage(m_stack);
    m_stack->addWidget(m_parentLinkPage);
    m_sidebar->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_MessageBoxInformation), tr_("cp.nav_parentlink")));
    m_rowParentLink = m_sidebar->count() - 1;

    m_familyPage = new ParentPage(m_stack);
    m_stack->addWidget(m_familyPage);
    m_sidebar->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_DialogYesButton), tr_("cp.nav_family")));
    m_rowFamily = m_sidebar->count() - 1;

    m_advancedPage = new SupabaseSetupPage(m_stack);
    m_stack->addWidget(m_advancedPage);
    m_sidebar->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_FileDialogListView), tr_("cp.nav_advanced")));
    m_rowAdvanced = m_sidebar->count() - 1;

    buildAboutPage();
    m_sidebar->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_MessageBoxQuestion), tr_("cp.nav_about")));
    m_rowAbout = m_sidebar->count() - 1;

    /*
     * Mục "Development" tách RIÊNG khỏi About - đây là nơi chuyên
     * cho thông tin build/kỹ thuật (chế độ core Stable/Experimental
     * đang chạy...), không lẫn vào thông tin giới thiệu app chung
     * chung ở About.
     */
    buildDevelopmentPage();
    m_sidebar->addItem(new QListWidgetItem(style()->standardIcon(QStyle::SP_DialogHelpButton), tr_("cp.nav_development")));
    m_rowDevelopment = m_sidebar->count() - 1;

    connect(m_sidebar, &QListWidget::currentRowChanged, this, &ControlPanelWindow::onSidebarRowChanged);

    applyStylesheet();
    refreshSidebarVisibility();
    refreshOverview();

    m_sidebar->setCurrentRow(m_rowOverview);
}

void ControlPanelWindow::buildOverviewPage()
{
    m_overviewPage = new QWidget;

    m_overviewGreeting = new QLabel(m_overviewPage);
    m_overviewGreeting->setStyleSheet("font-size: 20px; font-weight: 700;");

    m_overviewSubtitle = new QLabel(m_overviewPage);
    m_overviewSubtitle->setWordWrap(true);
    m_overviewSubtitle->setStyleSheet("color: #7e9ac0;");

    m_overviewLoginBtn = new QPushButton(tr_("cp.overview_login_cta"), m_overviewPage);
    m_overviewLoginBtn->setProperty("accent", true);
    connect(m_overviewLoginBtn, &QPushButton::clicked, this, [this]() {
        m_sidebar->setCurrentRow(m_rowAccount);
    });

    // ---- Trạng thái theo dõi ----

    auto *statusBox = new QFrame(m_overviewPage);
    statusBox->setObjectName("card");
    auto *statusBoxLayout = new QVBoxLayout(statusBox);

    m_overviewStatus = new QLabel(statusBox);
    m_overviewPauseBtn = new QPushButton(statusBox);
    connect(m_overviewPauseBtn, &QPushButton::clicked, this, &ControlPanelWindow::pauseToggleRequested);

    statusBoxLayout->addWidget(m_overviewStatus);
    statusBoxLayout->addWidget(m_overviewPauseBtn, 0, Qt::AlignLeft);

    /*
     * SẮP XẾP LẠI (theo yêu cầu, giống thứ tự bên dashboard Go):
     * bước 1 - thời gian dùng máy hôm nay TRƯỚC TIÊN (kèm icon từng
     * app, xem iconForProcess); bước 2 - danh sách Machines; bước 3
     * - mọi thứ còn lại (lối tắt, giới thiệu) xuống dưới cùng -
     * trước đây thứ tự ngược lại (giới thiệu/lối tắt lên trước
     * "Today").
     */

    // ---- 1. Hôm nay (kèm icon từng app) ----

    auto *todayBox = new QFrame(m_overviewPage);
    todayBox->setObjectName("card");
    auto *todayBoxLayout = new QVBoxLayout(todayBox);

    auto *todayTitle = new QLabel(tr_("cp.overview_today_title"), todayBox);
    todayTitle->setStyleSheet("font-weight: 600;");

    m_overviewTodayList = new QVBoxLayout();
    m_overviewTodayList->setSpacing(8);

    m_overviewTodayEmpty = new QLabel(todayBox);
    m_overviewTodayEmpty->setStyleSheet("color: #4a6584;");

    todayBoxLayout->addWidget(todayTitle);
    todayBoxLayout->addLayout(m_overviewTodayList);
    todayBoxLayout->addWidget(m_overviewTodayEmpty);

    // ---- 2. Machines ----

    auto *machinesBox = new QFrame(m_overviewPage);
    machinesBox->setObjectName("card");
    auto *machinesBoxLayout = new QVBoxLayout(machinesBox);

    auto *machinesTitle = new QLabel(tr_("cp.overview_machines_title"), machinesBox);
    machinesTitle->setStyleSheet("font-weight: 600;");

    m_overviewMachinesList = new QVBoxLayout();
    m_overviewMachinesList->setSpacing(8);

    m_overviewMachinesEmpty = new QLabel(machinesBox);
    m_overviewMachinesEmpty->setWordWrap(true);
    m_overviewMachinesEmpty->setStyleSheet("color: #4a6584;");

    machinesBoxLayout->addWidget(machinesTitle);
    machinesBoxLayout->addLayout(m_overviewMachinesList);
    machinesBoxLayout->addWidget(m_overviewMachinesEmpty);

    // ---- 3a. Lối tắt nhanh ----

    auto *quickBox = new QFrame(m_overviewPage);
    quickBox->setObjectName("card");
    auto *quickBoxLayout = new QVBoxLayout(quickBox);

    auto *quickTitle = new QLabel(tr_("cp.overview_quick_links"), quickBox);
    quickTitle->setStyleSheet("font-weight: 600;");

    auto *openDashboardBtn = new QPushButton(tr_("cp.overview_open_dashboard"), quickBox);
    connect(openDashboardBtn, &QPushButton::clicked, this, &ControlPanelWindow::openWebDashboard);

    quickBoxLayout->addWidget(quickTitle);
    quickBoxLayout->addWidget(openDashboardBtn, 0, Qt::AlignLeft);

    // ---- 3b. Giới thiệu ngắn (giúp người mới làm quen) ----

    auto *introBox = new QFrame(m_overviewPage);
    introBox->setObjectName("card");
    auto *introBoxLayout = new QVBoxLayout(introBox);

    auto *introTitle = new QLabel(tr_("cp.overview_first_run_title"), introBox);
    introTitle->setStyleSheet("font-weight: 600;");

    auto *introBody = new QLabel(tr_("cp.overview_first_run_body"), introBox);
    introBody->setWordWrap(true);
    introBody->setStyleSheet("color: #b9c8de;");

    introBoxLayout->addWidget(introTitle);
    introBoxLayout->addWidget(introBody);

    auto *contentLayout = new QVBoxLayout();
    contentLayout->addWidget(m_overviewGreeting);
    contentLayout->addWidget(m_overviewSubtitle);
    contentLayout->addWidget(m_overviewLoginBtn, 0, Qt::AlignLeft);
    contentLayout->addSpacing(12);
    contentLayout->addWidget(statusBox);
    contentLayout->addWidget(todayBox);
    contentLayout->addWidget(machinesBox);
    contentLayout->addWidget(quickBox);
    contentLayout->addWidget(introBox);
    contentLayout->addStretch();

    // BỌC QScrollArea (FIX: trước đây Overview KHÔNG cuộn được - nếu
    // "Today so far" có nhiều dòng và cửa sổ không đủ cao, phần nội
    // dung phía dưới bị cắt mất/đè lên nhau, không có cách nào xem
    // hết - đúng như bạn phản ánh "không hiển thị hết nội dung").
    auto *content = new QWidget;
    content->setLayout(contentLayout);

    auto *scroll = new QScrollArea(m_overviewPage);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidget(content);

    auto *outer = new QVBoxLayout(m_overviewPage);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addWidget(scroll);
}

void ControlPanelWindow::openWebDashboard()
{
    /*
     * FIX ("mở dashboard trên Qt không hoạt động, phải mở server
     * trước"): trước đây bấm nút này chỉ gọi thẳng
     * QDesktopServices::openUrl() - nếu dashboard-go (dashboard.exe)
     * chưa chạy sẵn, trình duyệt mở ra 1 trang lỗi "không kết nối
     * được" vì chẳng có gì đang lắng nghe cổng 5000 cả. Giờ: thử kết
     * nối cổng trước; nếu chưa có gì lắng nghe, tự khởi động
     * dashboard.exe (cùng thư mục cài đặt với tray agent) rồi đợi 1
     * chút cho server sẵn sàng trước khi mở trình duyệt.
     */
    const quint16 port = 5000;

    auto openBrowser = [port]() {
        QDesktopServices::openUrl(QUrl(QString("http://127.0.0.1:%1/").arg(port)));
    };

    QTcpSocket probe;
    probe.connectToHost(QHostAddress::LocalHost, port);

    if (probe.waitForConnected(150))
    {
        probe.disconnectFromHost();
        openBrowser();
        return;
    }

    QString exePath = QCoreApplication::applicationDirPath() + "/dashboard.exe";

    if (!QFile::exists(exePath))
    {
        QMessageBox::warning(
            this, "JustInTime",
            tr_("cp.overview_dashboard_missing").arg(exePath)
        );
        return;
    }

    QProcess::startDetached(exePath, {}, QCoreApplication::applicationDirPath());

    // Đợi server khởi động xong (thường <1s) rồi mới mở trình duyệt -
    // thử vài lần thay vì chờ cứng 1 khoảng thời gian cố định, để
    // không mở trình duyệt quá sớm (lỗi) hay quá muộn (chờ vô ích).
    QTimer *retryTimer = new QTimer(this);
    auto *attempts = new int(0);

    connect(retryTimer, &QTimer::timeout, this, [=]() mutable {
        QTcpSocket *retryProbe = new QTcpSocket(this);
        retryProbe->connectToHost(QHostAddress::LocalHost, port);

        if (retryProbe->waitForConnected(150))
        {
            retryProbe->disconnectFromHost();
            retryTimer->stop();
            retryTimer->deleteLater();
            openBrowser();
        }
        else if (++(*attempts) >= 20) // ~5s tổng cộng
        {
            retryTimer->stop();
            retryTimer->deleteLater();
            openBrowser(); // vẫn mở - có thể server chỉ chậm hơn dự kiến
        }

        retryProbe->deleteLater();
    });
    retryTimer->start(250);
}

void ControlPanelWindow::buildAboutPage()
{
    auto *page = new QWidget;

    auto *icon = new QLabel(page);
    icon->setPixmap(qApp->style()->standardIcon(QStyle::SP_ComputerIcon).pixmap(48, 48));

    auto *title = new QLabel("JustInTime", page);
    title->setStyleSheet("font-size: 20px; font-weight: 700;");

    auto *version = new QLabel(QString("v%1").arg(QString::fromUtf8(APP_VERSION)), page);
    version->setStyleSheet("color: #7e9ac0;");

    auto *body = new QLabel(
        tr_("about.body").arg(APP_VERSION, APP_PUBLISHER, APP_WEBSITE),
        page
    );
    body->setWordWrap(true);
    body->setTextInteractionFlags(Qt::TextBrowserInteraction);
    body->setOpenExternalLinks(true);

    auto *headRow = new QHBoxLayout;
    headRow->addWidget(icon);
    auto *headTextCol = new QVBoxLayout;
    headTextCol->addWidget(title);
    headTextCol->addWidget(version);
    headRow->addLayout(headTextCol);
    headRow->addStretch();

    auto *layout = new QVBoxLayout(page);
    layout->addLayout(headRow);
    layout->addSpacing(16);
    layout->addWidget(body);
    layout->addStretch();

    m_stack->addWidget(page);
}

void ControlPanelWindow::buildDevelopmentPage()
{
    auto *page = new QWidget;

    auto *heading = new QLabel(tr_("cp.nav_development"), page);
    heading->setStyleSheet("font-size: 16px; font-weight: 600;");

    auto *subtitle = new QLabel(tr_("dev.subtitle"), page);
    subtitle->setWordWrap(true);
    subtitle->setStyleSheet("color: #7e9ac0; font-size: 12px;");

    /*
     * Đọc trực tiếp JUSTINTIME_CORE_MODE - macro do CMakeLists.txt
     * định nghĩa lúc build (target_compile_definitions), phản ánh
     * ĐÚNG file .exe đang chạy, không đổi được lúc runtime.
     */
#ifdef JUSTINTIME_CORE_MODE
    const QString coreMode = JUSTINTIME_CORE_MODE;
#else
    const QString coreMode = "STABLE";
#endif
    const bool isExperimental = (coreMode == "EXPERIMENTAL");

    auto *coreBox = new QFrame(page);
    coreBox->setObjectName("card");
    auto *coreBoxLayout = new QVBoxLayout(coreBox);

    auto *coreBoxTitle = new QLabel(tr_("dev.core_mode_title"), coreBox);
    coreBoxTitle->setStyleSheet("font-weight: 600;");

    auto *coreBadge = new QLabel(
        isExperimental ? tr_("dev.badge_experimental") : tr_("dev.badge_stable"),
        coreBox
    );
    coreBadge->setStyleSheet(
        isExperimental
            ? "color: #f5a524; font-size: 12px; font-weight: 700; "
              "background: rgba(245,165,36,0.15); border: 1px solid rgba(245,165,36,0.4); "
              "border-radius: 6px; padding: 3px 10px;"
            : "color: #22c55e; font-size: 12px; font-weight: 700; "
              "background: rgba(34,197,94,0.15); border: 1px solid rgba(34,197,94,0.4); "
              "border-radius: 6px; padding: 3px 10px;"
    );
    coreBadge->setFixedWidth(coreBadge->sizeHint().width() + 4);

    auto *coreDesc = new QLabel(
        isExperimental ? tr_("dev.core_desc_experimental") : tr_("dev.core_desc_stable"),
        coreBox
    );
    coreDesc->setWordWrap(true);
    coreDesc->setStyleSheet("color: #b9c8de; font-size: 12px;");

    coreBoxLayout->addWidget(coreBoxTitle);
    coreBoxLayout->addWidget(coreBadge, 0, Qt::AlignLeft);
    coreBoxLayout->addWidget(coreDesc);

    auto *switchNote = new QFrame(page);
    switchNote->setObjectName("card");
    auto *switchNoteLayout = new QVBoxLayout(switchNote);

    auto *switchTitle = new QLabel(tr_("dev.switch_title"), switchNote);
    switchTitle->setStyleSheet("font-weight: 600;");

    auto *switchBody = new QLabel(tr_("dev.switch_body"), switchNote);
    switchBody->setWordWrap(true);
    switchBody->setTextInteractionFlags(Qt::TextSelectableByMouse);
    switchBody->setStyleSheet("color: #b9c8de; font-size: 12px; font-family: Consolas, monospace;");

    switchNoteLayout->addWidget(switchTitle);
    switchNoteLayout->addWidget(switchBody);

    auto *layout = new QVBoxLayout(page);
    layout->addWidget(heading);
    layout->addWidget(subtitle);
    layout->addSpacing(8);
    layout->addWidget(coreBox);
    layout->addWidget(switchNote);
    layout->addStretch();

    m_stack->addWidget(page);
}

void ControlPanelWindow::setPaused(bool paused)
{
    m_paused = paused;

    m_overviewStatus->setText(
        paused ? tr_("cp.overview_status_paused") : tr_("cp.overview_status_tracking")
    );
    m_overviewPauseBtn->setText(
        paused ? tr_("cp.overview_resume_btn") : tr_("cp.overview_pause_btn")
    );
}

// Xoá hết widget con hiện có trong 1 QVBoxLayout trước khi vẽ lại
// (dùng cho cả danh sách "Today" và "Machines" - cả 2 đều tạo lại
// toàn bộ danh sách mỗi lần refresh, đơn giản hơn nhiều so với diff
// từng dòng, và tần suất refresh không cao (mỗi lần mở trang) nên
// không đáng lo về hiệu năng).
static void clearLayout(QVBoxLayout *layout)
{
    QLayoutItem *item;
    while ((item = layout->takeAt(0)) != nullptr)
    {
        if (item->widget())
            item->widget()->deleteLater();
        delete item;
    }
}

void ControlPanelWindow::refreshOverview()
{
    AuthSession session;
    auth_get_session(&session);

    if (session.logged_in)
    {
        m_overviewGreeting->setText(
            QString("%1, %2").arg(tr_("cp.overview_greeting_in")).arg(QString::fromUtf8(session.email))
        );
        m_overviewSubtitle->hide();
        m_overviewLoginBtn->hide();
    }
    else
    {
        m_overviewGreeting->setText(tr_("cp.overview_greeting_out"));
        m_overviewSubtitle->setText(tr_("cp.overview_not_logged_in"));
        m_overviewSubtitle->show();
        m_overviewLoginBtn->show();
    }

    setPaused(m_paused);

    // ---- 1. Hôm nay: danh sách có icon (thay vì 1 khối text) ----

    clearLayout(m_overviewTodayList);

    DailyAppSummaryEntry entries[DAILY_APP_SUMMARY_MAX];
    int entryCount = db_get_today_app_summary(entries, DAILY_APP_SUMMARY_MAX);

    m_overviewTodayEmpty->setVisible(entryCount == 0);
    m_overviewTodayEmpty->setText(tr_("cp.overview_no_data"));

    for (int i = 0; i < entryCount; i++)
    {
        auto *row = new QWidget(m_overviewPage);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(10);

        QString processName = QString::fromWCharArray(entries[i].process_name);

        auto *iconLabel = new QLabel(iconForProcess(processName), row);
        iconLabel->setStyleSheet("font-size: 16px;");
        iconLabel->setFixedWidth(24);

        auto *nameLabel = new QLabel(prettyProcessName(processName), row);
        nameLabel->setStyleSheet("color: #eef4fb;");

        auto *durationLabel = new QLabel(formatHMS(entries[i].total_seconds), row);
        durationLabel->setStyleSheet("color: #7e9ac0; font-family: Consolas, monospace;");

        rowLayout->addWidget(iconLabel);
        rowLayout->addWidget(nameLabel, 1);
        rowLayout->addWidget(durationLabel);

        m_overviewTodayList->addWidget(row);
    }

    // ---- 2. Machines (giống mục "Machines" bên dashboard Go) ----

    clearLayout(m_overviewMachinesList);

    MachineHeartbeat machines[MACHINES_MAX];
    int machineCount = session.logged_in ? machines_list(machines, MACHINES_MAX) : 0;

    char selfDeviceId[64] = {0};
    get_device_id(selfDeviceId, sizeof(selfDeviceId));

    if (!session.logged_in)
        m_overviewMachinesEmpty->setText(tr_("cp.overview_machines_login_required"));
    else if (machineCount == 0)
        m_overviewMachinesEmpty->setText(tr_("cp.overview_machines_none"));

    m_overviewMachinesEmpty->setVisible(machineCount == 0);

    for (int i = 0; i < machineCount; i++)
    {
        auto *row = new QWidget(m_overviewPage);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setSpacing(10);

        bool isSelf = strcmp(machines[i].device_id, selfDeviceId) == 0;

        auto *dot = new QLabel("●", row);
        dot->setStyleSheet(isSelf ? "color: #22c55e;" : "color: #4a6584;");
        dot->setFixedWidth(14);

        QString label = QString::fromUtf8(machines[i].hostname);
        if (label.isEmpty())
            label = QString::fromUtf8(machines[i].device_id);
        if (isSelf)
            label += " " + tr_("cp.overview_machines_this_device");

        auto *nameLabel = new QLabel(label, row);
        nameLabel->setStyleSheet("color: #eef4fb;");

        rowLayout->addWidget(dot);
        rowLayout->addWidget(nameLabel, 1);

        m_overviewMachinesList->addWidget(row);
    }
}

void ControlPanelWindow::onLoggedInOrOut()
{
    refreshOverview();
    m_parentLinkPage->refresh();
    m_familyPage->refresh();
    emit accountChanged();
}

void ControlPanelWindow::onSettingsSaved()
{
    refreshSidebarVisibility();
    refreshOverview();
    emit settingsChanged();
}

void ControlPanelWindow::refreshSidebarVisibility()
{
    AppSettings s;
    settings_get(&s);

    const bool isChild  = (s.app_role != APP_ROLE_PARENT);
    const bool isParent = (s.app_role == APP_ROLE_PARENT);

    m_sidebar->item(m_rowParentLink)->setHidden(!isChild);
    m_sidebar->item(m_rowFamily)->setHidden(!isParent);

    /*
     * Nếu trang đang chọn vừa bị ẩn (đổi role trong lúc đang xem),
     * nhảy về Overview cho an toàn thay vì để 1 trang trống hiện ra.
     */
    QListWidgetItem *current = m_sidebar->currentItem();
    if (current && current->isHidden())
        m_sidebar->setCurrentRow(m_rowOverview);
}

int ControlPanelWindow::pageToRow(Page page) const
{
    switch (page)
    {
        case PageOverview:   return m_rowOverview;
        case PageAccount:    return m_rowAccount;
        case PageSettings:   return m_rowSettings;
        case PageRemoteView: return m_rowRemoteView;
        case PageParentLink: return m_rowParentLink;
        case PageFamily:     return m_rowFamily;
        case PageAdvanced:   return m_rowAdvanced;
        case PageAbout:      return m_rowAbout;
        case PageDevelopment: return m_rowDevelopment;
    }
    return m_rowOverview;
}

void ControlPanelWindow::openTo(Page page)
{
    refreshSidebarVisibility();

    if (page == PageOverview)
        refreshOverview();
    else if (page == PageAccount)
        m_loginPage->refresh();
    else if (page == PageParentLink)
        m_parentLinkPage->refresh();
    else if (page == PageFamily)
        m_familyPage->refresh();

    m_sidebar->setCurrentRow(pageToRow(page));

    show();
    raise();
    activateWindow();
}

void ControlPanelWindow::closeEvent(QCloseEvent *event)
{
    hide();
    event->ignore();
}

void ControlPanelWindow::onSidebarRowChanged(int row)
{
    if (row < 0)
        return;

    // Chỉ mục hàng trong sidebar khớp 1-1 với chỉ mục widget trong
    // stack theo đúng thứ tự đã addWidget()/addItem() ở constructor.
    m_stack->setCurrentIndex(row);

    // Hiệu ứng chuyển trang (FIX "không có animation"): fade-in nhẹ
    // cho trang vừa hiện ra - dùng QGraphicsOpacityEffect (rẻ, không
    // cần vẽ lại phức tạp) + QPropertyAnimation. Hiệu ứng cũ (widget
    // effect) của trang trước tự bị Qt dọn khi widget đó bị thay
    // effect mới hoặc xoá, không cần tự quản lý vòng đời.
    QWidget *page = m_stack->currentWidget();
    if (page)
    {
        auto *effect = new QGraphicsOpacityEffect(page);
        page->setGraphicsEffect(effect);

        auto *anim = new QPropertyAnimation(effect, "opacity", page);
        anim->setDuration(180);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, effect, [page]() {
            // Bỏ effect sau khi chạy xong - để hiệu ứng opacity
            // không "ăn" hiệu năng vẽ lại của trang mọi lúc, chỉ lúc
            // chuyển trang mới cần.
            page->setGraphicsEffect(nullptr);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    if (row == m_rowOverview)
        refreshOverview();
    else if (row == m_rowAccount)
        m_loginPage->refresh();
    else if (row == m_rowParentLink)
        m_parentLinkPage->refresh();
    else if (row == m_rowFamily)
        m_familyPage->refresh();
}

void ControlPanelWindow::applyStylesheet()
{
    /*
     * Thiết kế lại toàn bộ (trước đây chỉ là 1 bảng QSS tối giản, các
     * dialog cũ trông như popup Win32 mặc định - viền cứng, không có
     * phân cấp thị giác, chữ đặc 1 cỡ). Giờ:
     *  - Bảng màu tối nhất quán với dashboard web (cùng #0c1c33/
     *    #1c3a5e/#2f7de1 - xem style.css) để 2 nơi trông cùng 1 sản
     *    phẩm.
     *  - Có khối thương hiệu riêng ở đầu sidebar (xem constructor).
     *  - Bo góc lớn hơn, đệm rộng hơn, viền dịu hơn (#16283f thay vì
     *    #1c3a5e ở input để đỡ "cứng") - cảm giác mềm/hiện đại hơn.
     *  - Trạng thái hover/focus/pressed rõ ràng cho MỌI control tương
     *    tác được, không chỉ nút bấm.
     */
    setStyleSheet(R"(
        QMainWindow, QWidget { background-color: #071527; color: #dce8f7; font-size: 10pt; }

        #sidebarWrap { background-color: #0a1830; border-right: 1px solid #142943; }
        #brandBar { background-color: transparent; border-bottom: 1px solid #142943; }
        #brandText { font-size: 14px; font-weight: 700; color: #ffffff; letter-spacing: 0.2px; }

        #stackWrap { background-color: #071527; }

        QListWidget {
            background-color: transparent;
            border: none;
            outline: none;
            padding: 10px 8px;
            font-size: 12.5px;
        }
        QListWidget::item {
            padding: 10px 12px;
            border-radius: 9px;
            margin: 1px 2px;
            color: #a9bcd6;
        }
        QListWidget::item:selected {
            background-color: #2f7de1;
            color: #ffffff;
            font-weight: 600;
        }
        QListWidget::item:hover:!selected {
            background-color: #142943;
            color: #e6edf7;
        }

        QFrame#card {
            background-color: #0c1c33;
            border: 1px solid #16283f;
            border-radius: 14px;
            padding: 16px;
            margin-bottom: 14px;
        }

        QLabel { background-color: transparent; }

        QLineEdit, QSpinBox, QComboBox {
            background-color: #0a1728;
            border: 1px solid #1e3654;
            border-radius: 8px;
            padding: 7px 10px;
            color: #eef4fb;
            selection-background-color: #2f7de1;
        }
        QLineEdit:hover, QSpinBox:hover, QComboBox:hover { border-color: #2a4a72; }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: #5aa9ff; }
        QLineEdit:disabled, QSpinBox:disabled, QComboBox:disabled { color: #4a6584; }
        QComboBox::drop-down { border: none; width: 22px; }
        QComboBox QAbstractItemView {
            background-color: #0c1c33;
            border: 1px solid #1e3654;
            selection-background-color: #2f7de1;
            outline: none;
        }

        QCheckBox { spacing: 8px; }
        QCheckBox::indicator {
            width: 16px; height: 16px;
            border: 1px solid #2a4a72;
            border-radius: 4px;
            background-color: #0a1728;
        }
        QCheckBox::indicator:checked {
            background-color: #2f7de1;
            border-color: #2f7de1;
        }

        QPushButton {
            background-color: #142943;
            border: 1px solid #1e3654;
            border-radius: 8px;
            padding: 8px 16px;
            color: #dce8f7;
            font-weight: 500;
        }
        QPushButton:hover { background-color: #1c3a5e; border-color: #2a4a72; }
        QPushButton:pressed { background-color: #10233d; }
        QPushButton:disabled { color: #4a6584; border-color: #16283f; }
        QPushButton[accent="true"] {
            background-color: #2f7de1;
            border: 1px solid #2f7de1;
            color: white;
            font-weight: 600;
            padding: 9px 20px;
        }
        QPushButton[accent="true"]:hover { background-color: #4a93f5; border-color: #4a93f5; }
        QPushButton[accent="true"]:pressed { background-color: #2568c4; }

        QTableWidget {
            background-color: #0a1728;
            border: 1px solid #16283f;
            border-radius: 10px;
            gridline-color: #16283f;
            alternate-background-color: #0c1c33;
            selection-background-color: #1c3a5e;
            selection-color: #ffffff;
        }
        QTableWidget::item { padding: 6px; }
        QHeaderView::section {
            background-color: #0c1c33;
            border: none;
            border-bottom: 1px solid #1e3654;
            padding: 8px;
            color: #7e9ac0;
            font-weight: 600;
            font-size: 10.5px;
            text-transform: uppercase;
        }

        QGroupBox {
            border: 1px solid #16283f;
            border-radius: 12px;
            margin-top: 14px;
            padding-top: 18px;
            font-weight: 600;
            color: #b9c8de;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            left: 12px;
            padding: 0 6px;
            color: #5aa9ff;
        }

        QScrollBar:vertical {
            background: transparent;
            width: 10px;
            margin: 0;
        }
        QScrollBar::handle:vertical {
            background: #1e3654;
            border-radius: 5px;
            min-height: 24px;
        }
        QScrollBar::handle:vertical:hover { background: #2a4a72; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }

        QMessageBox { background-color: #0c1c33; }
    )");
}
