// loginpage.h
//
// Trước đây là logindialog.h (QDialog). Giờ là 1 trang nhúng trong
// ControlPanelWindow - trang "Account". Sau khi đăng nhập/đăng ký
// thành công, phát signal loggedIn() thay vì tự đóng cửa sổ (không
// còn khái niệm "đóng" vì đây không phải dialog riêng nữa).
#pragma once

#include <QWidget>

class QLineEdit;
class QPushButton;
class QLabel;
class QStackedWidget;

class LoginPage : public QWidget
{
    Q_OBJECT

public:
    explicit LoginPage(QWidget *parent = nullptr);

    /*
     * Gọi lại mỗi khi trang này được hiện ra (ControlPanelWindow gọi
     * trước khi switch tới trang này) để cập nhật đúng trạng thái
     * đăng nhập hiện tại (có thể đã đổi từ nơi khác).
     */
    void refresh();

signals:
    void loggedIn();
    void loggedOut();

private slots:
    void onLoginClicked();
    void onRegisterClicked();
    void onLogoutClicked();

private:
    void attempt(bool isRegister);

    QStackedWidget *m_stack;

    // Trang "chưa đăng nhập"
    QLineEdit   *m_emailEdit;
    QLineEdit   *m_passwordEdit;
    QPushButton *m_loginBtn;
    QPushButton *m_registerBtn;

    // Trang "đã đăng nhập"
    QLabel      *m_loggedInAsLabel;
    QPushButton *m_logoutBtn;
};
