#include "ui/settingsdialog.h"
#include "ui/asyncutil.h"
#include "cloud.h"
#include "dashsession.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace jit::dash::ui {

namespace {
QLabel* msgLabel(QWidget* parent)
{
    auto* l = new QLabel(parent);
    l->setWordWrap(true);
    l->hide();
    return l;
}
void showMsg(QLabel* l, const QString& text, bool isError)
{
    l->setStyleSheet(isError ? "color:#ef4444;" : "color:#22c55e;");
    l->setText(text);
    l->show();
}
} // namespace

SettingsDialog::SettingsDialog(bool isLoggedIn, std::function<void()> onLoggedOut, QWidget* parent)
    : QDialog(parent), onLoggedOut_(std::move(onLoggedOut))
{
    setWindowTitle("Settings");
    setMinimumWidth(420);

    auto* tabs = new QTabWidget(this);

    // ---------------------------------------------------------- Appearance --
    auto* appearanceTab = new QWidget;
    auto* appearanceForm = new QFormLayout(appearanceTab);

    fontBox_ = new QComboBox; fontBox_->addItems({"sans", "mono", "serif"});
    langBox_ = new QComboBox; langBox_->addItems({"en", "vi"});
    timeFormatBox_ = new QComboBox; timeFormatBox_->addItems({"24h", "12h"});
    periodBox_ = new QComboBox; periodBox_->addItems({"today", "yesterday", "this_week", "last_week"});
    cpuThreshold_ = new QSpinBox; cpuThreshold_->setRange(1, 100); cpuThreshold_->setSuffix("%");
    ramThreshold_ = new QSpinBox; ramThreshold_->setRange(1, 100); ramThreshold_->setSuffix("%");
    diskThreshold_ = new QSpinBox; diskThreshold_->setRange(1, 100); diskThreshold_->setSuffix("%");

    appearanceForm->addRow("Font", fontBox_);
    appearanceForm->addRow("Language", langBox_);
    appearanceForm->addRow("Time format", timeFormatBox_);
    appearanceForm->addRow("Default period", periodBox_);
    appearanceForm->addRow("CPU alert threshold", cpuThreshold_);
    appearanceForm->addRow("RAM alert threshold", ramThreshold_);
    appearanceForm->addRow("Disk alert threshold", diskThreshold_);

    appearanceMsg_ = msgLabel(appearanceTab);
    auto* saveAppearanceBtn = new QPushButton("Save");
    connect(saveAppearanceBtn, &QPushButton::clicked, this, &SettingsDialog::saveAppearance);
    appearanceForm->addRow(appearanceMsg_);
    appearanceForm->addRow(saveAppearanceBtn);

    tabs->addTab(appearanceTab, "Appearance");
    loadInto(loadDashSettings());

    // ------------------------------------------------------------ Password --
    if (isLoggedIn)
    {
        auto* passTab = new QWidget;
        auto* passForm = new QFormLayout(passTab);
        newPassword_ = new QLineEdit; newPassword_->setEchoMode(QLineEdit::Password);
        confirmPassword_ = new QLineEdit; confirmPassword_->setEchoMode(QLineEdit::Password);
        passForm->addRow("New password", newPassword_);
        passForm->addRow("Confirm password", confirmPassword_);
        passwordMsg_ = msgLabel(passTab);
        auto* changeBtn = new QPushButton("Change Password");
        connect(changeBtn, &QPushButton::clicked, this, &SettingsDialog::changePassword);
        passForm->addRow(passwordMsg_);
        passForm->addRow(changeBtn);
        tabs->addTab(passTab, "Password");
    }
    else
    {
        newPassword_ = nullptr; confirmPassword_ = nullptr; passwordMsg_ = nullptr;
    }

    // ------------------------------------------------------------- Account --
    auto* accountTab = new QWidget;
    auto* accountLayout = new QVBoxLayout(accountTab);
    accountMsg_ = msgLabel(accountTab);
    accountLayout->addWidget(accountMsg_);

    if (isLoggedIn)
    {
        auto* logoutBtn = new QPushButton("Log Out");
        auto* logoutAllBtn = new QPushButton("Log Out Everywhere");
        connect(logoutBtn, &QPushButton::clicked, this, [this] { logout(false); });
        connect(logoutAllBtn, &QPushButton::clicked, this, [this] {
            if (QMessageBox::question(this, "Log Out Everywhere",
                    "This will sign this account out on ALL devices. Continue?") == QMessageBox::Yes)
                logout(true);
        });
        accountLayout->addWidget(logoutBtn);
        accountLayout->addWidget(logoutAllBtn);
    }
    else
    {
        accountLayout->addWidget(new QLabel("Not logged in."));
    }
    accountLayout->addStretch();
    tabs->addTab(accountTab, "Account");

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(tabs);
    layout->addWidget(buttons);
}

void SettingsDialog::loadInto(const DashSettings& s)
{
    fontBox_->setCurrentText(QString::fromStdString(s.font));
    langBox_->setCurrentText(QString::fromStdString(s.language));
    timeFormatBox_->setCurrentText(QString::fromStdString(s.timeFormat));
    periodBox_->setCurrentText(QString::fromStdString(s.defaultPeriod));
    cpuThreshold_->setValue(s.cpuThreshold);
    ramThreshold_->setValue(s.ramThreshold);
    diskThreshold_->setValue(s.diskThreshold);
}

void SettingsDialog::saveAppearance()
{
    DashSettings s = loadDashSettings(); // giữ nguyên geminiApiKey (không có UI cho nó ở bản này)
    s.font = fontBox_->currentText().toStdString();
    s.language = langBox_->currentText().toStdString();
    s.timeFormat = timeFormatBox_->currentText().toStdString();
    s.defaultPeriod = periodBox_->currentText().toStdString();
    s.cpuThreshold = cpuThreshold_->value();
    s.ramThreshold = ramThreshold_->value();
    s.diskThreshold = diskThreshold_->value();

    if (saveDashSettings(s))
        showMsg(appearanceMsg_, "Saved. Some changes take effect after reopening this window.", false);
    else
        showMsg(appearanceMsg_, "Failed to save settings.", true);
}

void SettingsDialog::changePassword()
{
    const QString pass = newPassword_->text(), confirm = confirmPassword_->text();
    if (pass != confirm)
    {
        showMsg(passwordMsg_, "Passwords do not match.", true);
        return;
    }
    if (pass.size() < 6)
    {
        showMsg(passwordMsg_, "Password must be at least 6 characters.", true);
        return;
    }

    const auto sess = loadDashSession();
    if (!sess)
    {
        showMsg(passwordMsg_, "Not logged in.", true);
        return;
    }

    const std::string token = sess->accessToken;
    const std::string newPass = pass.toStdString();

    runAsyncVoid(this,
        [token, newPass]() { cloud::Client(token).changePassword(newPass); },
        [this](const std::exception* e) {
            if (e) showMsg(passwordMsg_, QString::fromStdString(e->what()), true);
            else { showMsg(passwordMsg_, "Password changed.", false); newPassword_->clear(); confirmPassword_->clear(); }
        });
}

void SettingsDialog::logout(bool everywhere)
{
    const auto sess = loadDashSession();
    if (!sess)
        return;

    const std::string token = sess->accessToken;
    const std::string scope = everywhere ? "global" : "";

    runAsyncVoid(this,
        [token, scope]() { cloud::logout(token, scope); },
        [this](const std::exception* e) {
            // Dù server từ chối (mạng lỗi...) vẫn xoá phiên cục bộ - hành vi
            // giống hệt handleAuthLogout bên Go: ưu tiên để người dùng thoát
            // được ra khỏi tài khoản trên MÁY NÀY.
            (void)e;
            clearDashSession();
            showMsg(accountMsg_, "Logged out.", false);
            if (onLoggedOut_)
                onLoggedOut_();
            QTimer::singleShot(600, this, &QDialog::accept);
        });
}

} // namespace jit::dash::ui
