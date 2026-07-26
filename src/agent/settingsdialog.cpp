// settingsdialog.cpp

#include "settingsdialog.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSpinBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QComboBox>
#include <QPushButton>
#include <QMessageBox>
#include <QLabel>

#include <cstdio>

extern "C" {
#include "settings.h"
#include "error_codes.h"
}

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Settings");
    setFixedWidth(420);

    AppSettings s;
    settings_get(&s);

    m_syncInterval = new QSpinBox(this);
    m_syncInterval->setRange(5, 3600);
    m_syncInterval->setValue(s.sync_interval_sec);

    m_backupInterval = new QSpinBox(this);
    m_backupInterval->setRange(60, 86400);
    m_backupInterval->setValue(s.backup_interval_sec);

    m_summaryInterval = new QSpinBox(this);
    m_summaryInterval->setRange(30, 3600);
    m_summaryInterval->setValue(s.summary_interval_sec);

    m_minDuration = new QSpinBox(this);
    m_minDuration->setRange(0, 3600);
    m_minDuration->setValue(s.min_duration_sec);

    m_excludedProcesses = new QLineEdit(QString::fromUtf8(s.excluded_processes), this);
    m_excludedProcesses->setPlaceholderText("e.g. steam.exe,discord.exe");

    m_autostart = new QCheckBox("Start with Windows", this);
    m_autostart->setChecked(s.autostart_enabled != 0);

    m_roleCombo = new QComboBox(this);
    m_roleCombo->addItem("Con (Agent) - máy này được theo dõi/giới hạn", APP_ROLE_CHILD);
    m_roleCombo->addItem("Phụ huynh (Parent) - máy này xem/giới hạn máy con", APP_ROLE_PARENT);
    m_roleCombo->setCurrentIndex(s.app_role == APP_ROLE_PARENT ? 1 : 0);

    auto *roleNote = new QLabel(
        "Đổi vai trò không xoá dữ liệu đã theo dõi trên máy này. "
        "Vai trò \"Con\" là mặc định và giữ nguyên hành vi cũ.",
        this
    );
    roleNote->setWordWrap(true);
    roleNote->setStyleSheet("color: #7e9ac0; font-size: 11px;");

    auto *form = new QFormLayout;
    form->addRow("Vai trò máy này:", m_roleCombo);
    form->addRow(QString(), roleNote);
    form->addRow("Sync interval (seconds):", m_syncInterval);
    form->addRow("Backup interval (seconds):", m_backupInterval);
    form->addRow("Summary interval (seconds):", m_summaryInterval);
    form->addRow("Minimum duration to log (seconds):", m_minDuration);
    form->addRow("Excluded apps:", m_excludedProcesses);
    form->addRow(QString(), m_autostart);

    auto *saveBtn   = new QPushButton("Save", this);
    auto *cancelBtn = new QPushButton("Cancel", this);

    connect(saveBtn, &QPushButton::clicked, this, &SettingsDialog::onSave);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(saveBtn);
    btnRow->addWidget(cancelBtn);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addLayout(form);
    mainLayout->addSpacing(12);
    mainLayout->addLayout(btnRow);
}

void SettingsDialog::onSave()
{
    AppSettings s;
    settings_get(&s); /* giữ lại supabase_url/key hiện có */

    s.sync_interval_sec    = m_syncInterval->value();
    s.backup_interval_sec  = m_backupInterval->value();
    s.summary_interval_sec = m_summaryInterval->value();
    s.min_duration_sec     = m_minDuration->value();
    s.autostart_enabled    = m_autostart->isChecked() ? 1 : 0;
    s.app_role              = m_roleCombo->currentData().toInt();

    QByteArray excludedUtf8 = m_excludedProcesses->text().toUtf8();

    snprintf(
        s.excluded_processes,
        sizeof(s.excluded_processes),
        "%s",
        excludedUtf8.constData()
    );

    if (settings_update(&s))
    {
        QMessageBox::information(this, "JustInTime", "Settings saved.");
        accept();
    }
    else
    {
        QMessageBox::warning(
            this,
            "JustInTime",
            QString("[%1] Failed to save settings.").arg(ERR_SETTINGS_SAVE_FAIL)
        );
    }
}
