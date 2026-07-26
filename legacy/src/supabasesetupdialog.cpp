// supabasesetupdialog.cpp

#include "supabasesetupdialog.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>

#include <cstdio>

extern "C" {
#include "../include/settings.h"
#include "../include/error_codes.h"
}

SupabaseSetupDialog::SupabaseSetupDialog(QWidget *parent) : QDialog(parent)
{
    setWindowTitle("Supabase Setup");
    setFixedWidth(420);

    AppSettings s;
    settings_get(&s);

    m_urlEdit = new QLineEdit(QString::fromUtf8(s.supabase_url), this);
    m_keyEdit = new QLineEdit(QString::fromUtf8(s.supabase_key), this);

    auto *form = new QFormLayout;
    form->addRow("Supabase URL:", m_urlEdit);
    form->addRow("Publishable Key:", m_keyEdit);

    auto *saveBtn   = new QPushButton("Save", this);
    auto *cancelBtn = new QPushButton("Cancel", this);

    connect(saveBtn, &QPushButton::clicked, this, &SupabaseSetupDialog::onSave);
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

void SupabaseSetupDialog::onSave()
{
    AppSettings s;
    settings_get(&s);

    QByteArray urlUtf8 = m_urlEdit->text().toUtf8();
    QByteArray keyUtf8 = m_keyEdit->text().toUtf8();

    snprintf(s.supabase_url, sizeof(s.supabase_url), "%s", urlUtf8.constData());
    snprintf(s.supabase_key, sizeof(s.supabase_key), "%s", keyUtf8.constData());

    if (settings_update(&s))
    {
        QMessageBox::information(this, "JustInTime", "Supabase configuration saved.");
        accept();
    }
    else
    {
        QMessageBox::warning(
            this,
            "JustInTime",
            QString("[%1] Failed to save configuration.").arg(ERR_SETTINGS_SAVE_FAIL)
        );
    }
}
