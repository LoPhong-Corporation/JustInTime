// controlpanelwindow.h
//
// Cửa sổ điều khiển trung tâm (Control Panel) - gom TẤT CẢ các
// dialog rời rạc trước đây (Settings, Login, Remote View, Monitored
// By, Family/Parent, Supabase Setup) thành 1 QMainWindow duy nhất
// với sidebar + nội dung, thay vì mở từng popup riêng lẻ từ tray
// menu. Mục tiêu: dễ làm quen hơn - người dùng mới thấy ngay TOÀN
// BỘ các mục có thể cấu hình ở 1 chỗ, thay vì phải dò tìm trong
// menu chuột phải.
//
// Chỉ có DUY NHẤT 1 instance được tạo (xem TrayIcon) và dùng lại
// nhiều lần - gọi openTo() để hiện cửa sổ và nhảy thẳng tới 1 trang
// cụ thể (ví dụ khi bấm "Settings..." trong tray menu).
#pragma once

#include <QMainWindow>

class QCloseEvent;
class QListWidget;
class QListWidgetItem;
class QStackedWidget;
class QLabel;
class QPushButton;
class QVBoxLayout;

class LoginPage;
class SettingsPage;
class RemoteViewPage;
class ParentLinkPage;
class SupabaseSetupPage;
class ParentPage;

class ControlPanelWindow : public QMainWindow
{
    Q_OBJECT

public:
    enum Page
    {
        PageOverview = 0,
        PageAccount,
        PageSettings,
        PageRemoteView,
        PageParentLink,   // "Monitored By" - chỉ hiện khi role = CHILD
        PageFamily,       // "Family"       - chỉ hiện khi role = PARENT
        PageAdvanced,     // Supabase setup
        PageAbout,
        PageDevelopment   // build info: chế độ core Stable/Experimental...
    };

    explicit ControlPanelWindow(QWidget *parent = nullptr);

    /*
     * Hiện cửa sổ (raise + activate) và chuyển tới trang chỉ định.
     * Đây là điểm vào DUY NHẤT mà TrayIcon nên gọi - thay cho việc
     * mở từng QDialog riêng như trước.
     */
    void openTo(Page page);

    /*
     * Gọi từ TrayIcon mỗi khi trạng thái tạm dừng đổi (kể cả khi
     * đổi từ chính nút Pause/Resume trên trang Overview - xem
     * pauseToggleRequested()) để cập nhật lại chữ/label hiển thị.
     */
    void setPaused(bool paused);

protected:
    /*
     * Bấm nút "X" chỉ ẨN cửa sổ, KHÔNG thoát app - JustInTime vẫn
     * chạy nền ở khay hệ thống như trước (thoát hẳn phải dùng
     * "Exit" trong tray menu). Đây là hành vi chuẩn của app chạy
     * nền có tray icon, tránh người mới bấm X rồi tưởng app đã tắt.
     */
    void closeEvent(QCloseEvent *event) override;

signals:
    /*
     * Người dùng bấm nút Pause/Resume ở trang Overview - TrayIcon
     * là nơi thực sự SỞ HỮU trạng thái pause (đọc từ worker thread
     * qua std::atomic), nên ControlPanelWindow chỉ yêu cầu, không
     * tự đổi trạng thái.
     */
    void pauseToggleRequested();

    /*
     * Đăng nhập/đăng xuất thay đổi từ trang Account - TrayIcon nối
     * vào đây để làm mới menu chuột phải (tên hiển thị, mục Login/
     * Logout) ngay lập tức, không cần đợi lần click icon tiếp theo.
     */
    void accountChanged();

    /*
     * Cài đặt (vai trò, ngôn ngữ...) vừa được lưu từ trang Settings -
     * TrayIcon nối vào đây để làm mới menu chuột phải ngay (ẩn/hiện
     * "Monitored By"/"Family", đổi ngôn ngữ nhãn menu...).
     */
    void settingsChanged();

private slots:
    void onSidebarRowChanged(int row);
    void onLoggedInOrOut();
    void onSettingsSaved();
    void openWebDashboard();

private:
    void buildOverviewPage();
    void buildAboutPage();
    void buildDevelopmentPage();
    void applyStylesheet();
    void refreshSidebarVisibility();
    void refreshOverview();
    int  pageToRow(Page page) const;

    QListWidget    *m_sidebar;
    QStackedWidget *m_stack;

    // Widget của trang Overview (tự viết riêng, không tách file
    // vì khá ngắn và gắn chặt với trạng thái tổng thể của cửa sổ).
    QWidget  *m_overviewPage      = nullptr;
    QLabel   *m_overviewGreeting  = nullptr;
    QLabel   *m_overviewSubtitle  = nullptr;
    QLabel   *m_overviewStatus    = nullptr;
    QPushButton *m_overviewLoginBtn = nullptr;
    QPushButton *m_overviewPauseBtn = nullptr;
    QVBoxLayout *m_overviewTodayList    = nullptr; // 1 dòng/app, có icon
    QLabel      *m_overviewTodayEmpty   = nullptr;
    QVBoxLayout *m_overviewMachinesList = nullptr; // 1 dòng/máy, giống Machines bên Go
    QLabel      *m_overviewMachinesEmpty = nullptr;
    bool      m_paused            = false;

    LoginPage          *m_loginPage;
    SettingsPage       *m_settingsPage;
    RemoteViewPage     *m_remoteViewPage;
    ParentLinkPage     *m_parentLinkPage;
    SupabaseSetupPage  *m_advancedPage;
    ParentPage         *m_familyPage;

    // Chỉ mục hàng sidebar tương ứng với từng Page - vì Monitored
    // By/Family bị ẩn tuỳ role nên số hàng thực tế thay đổi động,
    // không thể dùng thẳng enum Page làm chỉ số hàng.
    int m_rowOverview   = -1;
    int m_rowAccount    = -1;
    int m_rowSettings   = -1;
    int m_rowRemoteView = -1;
    int m_rowParentLink = -1;
    int m_rowFamily     = -1;
    int m_rowAdvanced   = -1;
    int m_rowAbout      = -1;
    int m_rowDevelopment = -1;
};
