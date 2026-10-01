#include "ui/logindialog.h"
#include "ui/asyncutil.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QDialogButtonBox>

namespace jit::dash::ui {

LoginDialog::LoginDialog(QWidget* parent) : QDialog(parent)
{
    setWindowTitle("JustInTime - Log In");
    setMinimumWidth(360);

    email_ = new QLineEdit(this);
    password_ = new QLineEdit(this);
    password_->setEchoMode(QLineEdit::Password);
    error_ = new QLabel(this);
    error_->setStyleSheet("color:#ef4444;");
    error_->setWordWrap(true);
    error_->hide();

    auto* form = new QFormLayout;
    form->addRow("Email", email_);
    form->addRow("Password", password_);

    loginBtn_ = new QPushButton("Log In", this);
    loginBtn_->setDefault(true);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    buttons->addButton(loginBtn_, QDialogButtonBox::AcceptRole);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(loginBtn_, &QPushButton::clicked, this, &LoginDialog::doLogin);
    connect(password_, &QLineEdit::returnPressed, this, &LoginDialog::doLogin);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(error_);
    layout->addWidget(buttons);
}

void LoginDialog::doLogin()
{
    const std::string email = email_->text().trimmed().toStdString();
    const std::string password = password_->text().toStdString();

    if (email.empty() || password.empty())
    {
        error_->setText("Please enter both email and password.");
        error_->show();
        return;
    }

    error_->hide();
    loginBtn_->setEnabled(false);
    loginBtn_->setText("Logging in...");

    runAsync(this,
        [email, password]() { return cloud::login(email, password); },
        [this](cloud::Session* s, const std::exception* e) {
            loginBtn_->setEnabled(true);
            loginBtn_->setText("Log In");

            if (e)
            {
                error_->setText(QString::fromStdString(e->what()));
                error_->show();
                return;
            }

            session_ = *s;
            accept();
        });
}

} // namespace jit::dash::ui
