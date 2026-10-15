// loginpage.cpp

#include "loginpage.h"

#include <QLineEdit>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QLabel>
#include <QMessageBox>

extern "C" {
#include "auth.h"
#include "i18n.h"
}

static QString tr_(const char *key)
{
    return QString::fromUtf8(i18n_t(key));
}

LoginPage::LoginPage(QWidget *parent) : QWidget(parent)
{
    m_stack = new QStackedWidget(this);

    // ---------------- "Chưa đăng nhập" ----------------

    auto *loggedOutPage = new QWidget(this);

    auto *heading = new QLabel(tr_("login.heading"), loggedOutPage);
    heading->setStyleSheet("font-size: 16px; font-weight: 600;");

    auto *note = new QLabel(tr_("login.note"), loggedOutPage);
    note->setWordWrap(true);
    note->setStyleSheet("color: #7e9ac0; font-size: 11px;");

    m_emailEdit = new QLineEdit(loggedOutPage);
    m_passwordEdit = new QLineEdit(loggedOutPage);
    m_passwordEdit->setEchoMode(QLineEdit::Password);

    auto *form = new QFormLayout;
    form->addRow(tr_("login.email_label"), m_emailEdit);
    form->addRow(tr_("login.password_label"), m_passwordEdit);

    m_loginBtn    = new QPushButton(tr_("login.login_btn"), loggedOutPage);
    m_registerBtn = new QPushButton(tr_("login.signup_btn"), loggedOutPage);
    m_loginBtn->setProperty("accent", true);

    connect(m_loginBtn, &QPushButton::clicked, this, &LoginPage::onLoginClicked);
    connect(m_registerBtn, &QPushButton::clicked, this, &LoginPage::onRegisterClicked);

    auto *btnRow = new QHBoxLayout;
    btnRow->addWidget(m_loginBtn);
    btnRow->addWidget(m_registerBtn);
    btnRow->addStretch();

    auto *loggedOutLayout = new QVBoxLayout(loggedOutPage);
    loggedOutLayout->addWidget(heading);
    loggedOutLayout->addWidget(note);
    loggedOutLayout->addSpacing(8);
    loggedOutLayout->addLayout(form);
    loggedOutLayout->addLayout(btnRow);
    loggedOutLayout->addStretch();

    // ---------------- "Đã đăng nhập" ----------------

    auto *loggedInPage = new QWidget(this);

    auto *loggedInHeading = new QLabel(tr_("login.heading"), loggedInPage);
    loggedInHeading->setStyleSheet("font-size: 16px; font-weight: 600;");

    m_loggedInAsLabel = new QLabel(loggedInPage);
    m_loggedInAsLabel->setWordWrap(true);

    m_logoutBtn = new QPushButton(tr_("login.logout_btn"), loggedInPage);
    connect(m_logoutBtn, &QPushButton::clicked, this, &LoginPage::onLogoutClicked);

    auto *loggedInLayout = new QVBoxLayout(loggedInPage);
    loggedInLayout->addWidget(loggedInHeading);
    loggedInLayout->addSpacing(8);
    loggedInLayout->addWidget(m_loggedInAsLabel);
    loggedInLayout->addSpacing(12);
    loggedInLayout->addWidget(m_logoutBtn, 0, Qt::AlignLeft);
    loggedInLayout->addStretch();

    m_stack->addWidget(loggedOutPage); // index 0
    m_stack->addWidget(loggedInPage);  // index 1

    auto *outer = new QVBoxLayout(this);
    outer->addWidget(m_stack);

    refresh();
}

void LoginPage::refresh()
{
    AuthSession session;
    auth_get_session(&session);

    if (session.logged_in)
    {
        m_loggedInAsLabel->setText(
            tr_("login.logged_in_as").arg(QString::fromUtf8(session.email))
        );
        m_stack->setCurrentIndex(1);
    }
    else
    {
        m_emailEdit->clear();
        m_passwordEdit->clear();
        m_stack->setCurrentIndex(0);
    }
}

void LoginPage::onLoginClicked()
{
    attempt(false);
}

void LoginPage::onRegisterClicked()
{
    attempt(true);
}

void LoginPage::onLogoutClicked()
{
    if (
        QMessageBox::question(
            this,
            "JustInTime",
            tr_("login.logout_confirm")
        ) != QMessageBox::Yes
    )
        return;

    auth_logout();
    refresh();

    QMessageBox::information(
        this,
        "JustInTime",
        tr_("login.logout_done")
    );

    emit loggedOut();
}

void LoginPage::attempt(bool isRegister)
{
    QString email    = m_emailEdit->text().trimmed();
    QString password = m_passwordEdit->text();

    if (email.isEmpty() || password.isEmpty())
    {
        QMessageBox::warning(this, "JustInTime", tr_("login.enter_both"));
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
                tr_("login.success")
            );
            refresh();
            emit loggedIn();
        }
        else if (err[0] != '\0')
        {
            /* Đăng ký thành công nhưng cần xác nhận email */
            QMessageBox::information(this, "JustInTime", QString::fromUtf8(err));
        }
    }
    else
    {
        QMessageBox::warning(this, "JustInTime", QString::fromUtf8(err));
    }
}
