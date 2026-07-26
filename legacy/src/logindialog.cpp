// logindialog.cpp

#include "logindialog.h"

#include <QLineEdit>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>

extern "C" {
#include "../include/auth.h"
}

LoginDialog::LoginDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Log in / Sign up");
    setFixedWidth(380);

    m_emailEdit = new QLineEdit(this);
    m_passwordEdit = new QLineEdit(this);
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    auto *form = new QFormLayout;
    form->addRow("Email:", m_emailEdit);
    form->addRow("Password:", m_passwordEdit);

    m_loginBtn    = new QPushButton("Log in", this);
    m_registerBtn = new QPushButton("Sign up", this);
    m_cancelBtn   = new QPushButton("Cancel", this);

    connect(m_loginBtn, &QPushButton::clicked, this, &LoginDialog::onLoginClicked);
    connect(m_registerBtn, &QPushButton::clicked, this, &LoginDialog::onRegisterClicked);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_loginBtn);
    btnRow->addWidget(m_registerBtn);
    btnRow->addStretch();
    btnRow->addWidget(m_cancelBtn);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addSpacing(12);
    mainLayout->addLayout(btnRow);
}

void LoginDialog::onLoginClicked()
{
    attempt(false);
}

void LoginDialog::onRegisterClicked()
{
    attempt(true);
}

void LoginDialog::attempt(bool isRegister)
{
    QString email    = m_emailEdit->text().trimmed();
    QString password = m_passwordEdit->text();

    if (email.isEmpty() || password.isEmpty())
    {
        QMessageBox::warning(this, "JustInTime", "Please enter both email and password.");
        return;
    }

    QByteArray emailUtf8    = email.toUtf8();
    QByteArray passwordUtf8 = password.toUtf8();

    char err[512] = {0};
    int ok;

    if (isRegister)
        ok = auth_register(emailUtf8.constData(), passwordUtf8.constData(), err, sizeof(err));
    else
        ok = auth_login(emailUtf8.constData(), passwordUtf8.constData(), err, sizeof(err));

    if (ok)
    {
        if (auth_is_logged_in())
        {
            QMessageBox::information(
                this,
                "JustInTime",
                "Logged in successfully! Your data will now sync to the cloud."
            );
            accept();
        }
        else if (err[0] != '\0')
        {
            /* Đăng ký thành công nhưng cần xác nhận email */
            QMessageBox::information(this, "JustInTime", QString::fromUtf8(err));
            reject();
        }
    }
    else
    {
        QMessageBox::warning(this, "JustInTime", QString::fromUtf8(err));
    }
}
