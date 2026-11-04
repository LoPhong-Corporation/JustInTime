// supabasesetuppage.cpp

#include "supabasesetuppage.h"

#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QLabel>

#include <cstdio>

extern "C" {
#include "settings.h"
#include "error_codes.h"
#include "i18n.h"
}

static QString tr_(const char *key)
{
    return QString::fromUtf8(i18n_t(key));
}

SupabaseSetupPage::SupabaseSetupPage(QWidget *parent) : QWidget(parent)
{
    AppSettings s;
    settings_get(&s);

    auto *heading = new QLabel(tr_("supabase.heading"), this);
    heading->setStyleSheet("font-size: 16px; font-weight: 600;");

    auto *note = new QLabel(tr_("supabase.note"), this);
    note->setWordWrap(true);
    note->setStyleSheet("color: #7e9ac0; font-size: 11px;");

    m_urlEdit = new QLineEdit(QString::fromUtf8(s.supabase_url), this);
    m_keyEdit = new QLineEdit(QString::fromUtf8(s.supabase_key), this);

    auto *form = new QFormLayout;
    form->addRow(tr_("supabase.url_label"), m_urlEdit);
    form->addRow(tr_("supabase.key_label"), m_keyEdit);

    auto *saveBtn = new QPushButton(tr_("supabase.save_btn"), this);
    saveBtn->setProperty("accent", true);

    connect(saveBtn, &QPushButton::clicked, this, &SupabaseSetupPage::onSave);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(saveBtn);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(heading);
    mainLayout->addWidget(note);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(form);
    mainLayout->addStretch();
    mainLayout->addLayout(btnRow);
}

void SupabaseSetupPage::onSave()
{
    AppSettings s;
    settings_get(&s);

    QByteArray urlUtf8 = m_urlEdit->text().toUtf8();
    QByteArray keyUtf8 = m_keyEdit->text().toUtf8();

    snprintf(s.supabase_url, sizeof(s.supabase_url), "%s", urlUtf8.constData());
    snprintf(s.supabase_key, sizeof(s.supabase_key), "%s", keyUtf8.constData());

    if (settings_update(&s))
    {
        QMessageBox::information(this, "JustInTime", tr_("supabase.saved"));
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
