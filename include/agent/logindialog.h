// logindialog.h
#pragma once

#include <QDialog>

class QLineEdit;
class QPushButton;

class LoginDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LoginDialog(QWidget *parent = nullptr);

private slots:
    void onLoginClicked();
    void onRegisterClicked();

private:
    void attempt(bool isRegister);

    QLineEdit   *m_emailEdit;
    QLineEdit   *m_passwordEdit;
    QPushButton *m_loginBtn;
    QPushButton *m_registerBtn;
    QPushButton *m_cancelBtn;
};
