//
// settingsdialog.h
// Thay settings.html. 3 tab: Appearance (font/ngôn ngữ/định dạng giờ/
// khoảng mặc định/ngưỡng cảnh báo), Password (đổi mật khẩu), Account
// (đăng xuất / đăng xuất mọi nơi). KHÔNG có tab "AI key" (aiinsights
// chưa được cổng hoá - xem BAO_CAO_UI_CPP.md).
//
#ifndef UI_SETTINGSDIALOG_H
#define UI_SETTINGSDIALOG_H

#include "dashsettings.h"

#include <QDialog>
#include <QLabel>
#include <functional>

class QComboBox;
class QSpinBox;
class QLineEdit;

namespace jit::dash::ui {

class SettingsDialog : public QDialog
{
public:
    // onSettingsSaved/onLoggedOut notify MainWindow after persistent changes.
    SettingsDialog(bool isLoggedIn, std::function<void()> onLoggedOut,
                   std::function<void()> onSettingsSaved, QWidget* parent = nullptr);

private:
    QComboBox* fontBox_;
    QComboBox* langBox_;
    QComboBox* timeFormatBox_;
    QComboBox* periodBox_;
    QSpinBox* cpuThreshold_;
    QSpinBox* ramThreshold_;
    QSpinBox* diskThreshold_;
    QLabel* appearanceMsg_;

    QLineEdit* newPassword_;
    QLineEdit* confirmPassword_;
    QLabel* passwordMsg_;

    QLabel* accountMsg_;
    std::function<void()> onLoggedOut_;
    std::function<void()> onSettingsSaved_;

    void loadInto(const DashSettings& s);
    void saveAppearance();
    void changePassword();
    void logout(bool everywhere);
};

} // namespace jit::dash::ui

#endif
