// settingsdialog.h
#pragma once

#include <QDialog>

class QSpinBox;
class QLineEdit;
class QCheckBox;

class SettingsDialog : public QDialog
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

private slots:
    void onSave();

private:
    QSpinBox  *m_syncInterval;
    QSpinBox  *m_backupInterval;
    QSpinBox  *m_summaryInterval;
    QSpinBox  *m_minDuration;
    QLineEdit *m_excludedProcesses;
    QCheckBox *m_autostart;
};
