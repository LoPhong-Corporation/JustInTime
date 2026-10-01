//
// logindialog.h
// Thay login.html. Chỉ email/password (KHÔNG có nút "Đăng nhập bằng
// Google/Microsoft" như bản web - OAuth dựa vào redirect trình duyệt,
// không hợp với app native không có control trình duyệt nhúng; xem
// BAO_CAO_UI_CPP.md mục "chưa làm").
//
#ifndef UI_LOGINDIALOG_H
#define UI_LOGINDIALOG_H

#include "cloud.h"

#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>

namespace jit::dash::ui {

class LoginDialog : public QDialog
{
public:
    explicit LoginDialog(QWidget* parent = nullptr);

    // Có giá trị sau khi exec() trả về QDialog::Accepted.
    cloud::Session session() const { return session_; }

private:
    QLineEdit* email_;
    QLineEdit* password_;
    QLabel* error_;
    QPushButton* loginBtn_;
    cloud::Session session_;

    void doLogin();
};

} // namespace jit::dash::ui

#endif
