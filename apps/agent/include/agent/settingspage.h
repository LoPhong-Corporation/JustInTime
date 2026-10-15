// settingspage.h
//
// Trước đây là settingsdialog.h (QDialog, mở riêng lẻ từ tray menu).
// Giờ là 1 trang (QWidget) nhúng bên trong ControlPanelWindow, cùng
// logic lưu/đọc AppSettings y hệt như cũ - chỉ khác lớp nền tảng
// (QWidget thay vì QDialog) và không còn accept()/reject().
#pragma once

#include <QWidget>

class QSpinBox;
class QLineEdit;
class QCheckBox;
class QComboBox;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget *parent = nullptr);

signals:
    /*
     * Phát ra sau khi lưu thành công - ControlPanelWindow dùng để
     * làm mới các phần phụ thuộc vào role/ngôn ngữ (ví dụ ẩn/hiện
     * mục "Được giám sát bởi" / "Family" trong sidebar).
     */
    void settingsSaved();

private slots:
    void onSave();

private:
    QSpinBox  *m_syncInterval;
    QSpinBox  *m_backupInterval;
    QSpinBox  *m_summaryInterval;
    QSpinBox  *m_minDuration;
    QLineEdit *m_excludedProcesses;
    QCheckBox *m_autostart;
    QComboBox *m_roleCombo;
    QComboBox *m_languageCombo;
};
