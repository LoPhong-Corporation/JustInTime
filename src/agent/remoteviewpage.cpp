// remoteviewpage.cpp

#include "remoteviewpage.h"

#include <QCheckBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QFormLayout>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QApplication>
#include <QClipboard>
#include <QNetworkInterface>

extern "C" {
#include "settings.h"
#include "remoteview.h"
#include "error_codes.h"
#include "i18n.h"
}

static QString tr_(const char *key)
{
    return QString::fromUtf8(i18n_t(key));
}

#include <windows.h>
#include <wincrypt.h>
#include <cstdio>

#pragma comment(lib, "crypt32.lib")

static QString localIpAddress()
{
    for (const QHostAddress &addr : QNetworkInterface::allAddresses())
    {
        if (
            addr.protocol() == QAbstractSocket::IPv4Protocol &&
            !addr.isLoopback()
        )
        {
            return addr.toString();
        }
    }

    return QStringLiteral("YOUR_LOCAL_IP");
}

RemoteViewPage::RemoteViewPage(QWidget *parent) : QWidget(parent)
{
    AppSettings s;
    settings_get(&s);

    auto *heading = new QLabel(tr_("rview.heading"), this);
    heading->setStyleSheet("font-size: 16px; font-weight: 600;");

    m_enabledCheck = new QCheckBox(tr_("rview.enable_check"), this);
    m_enabledCheck->setChecked(s.remote_view_enabled != 0);

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(1024, 65535);
    m_portSpin->setValue(s.remote_view_port);

    m_tokenEdit = new QLineEdit(QString::fromUtf8(s.remote_view_token), this);
    m_tokenEdit->setReadOnly(true);

    auto *regenBtn = new QPushButton(tr_("rview.regenerate_btn"), this);
    auto *tokenRow = new QHBoxLayout;
    tokenRow->addWidget(m_tokenEdit);
    tokenRow->addWidget(regenBtn);

    m_urlPreview = new QLabel(this);
    m_urlPreview->setWordWrap(true);
    m_urlPreview->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *copyBtn = new QPushButton(tr_("rview.copy_btn"), this);

    auto *warningLabel = new QLabel(tr_("rview.warning"), this);
    warningLabel->setWordWrap(true);
    warningLabel->setStyleSheet("color: #b45309;");

    auto *form = new QFormLayout;
    form->addRow(m_enabledCheck);
    form->addRow(tr_("rview.port_label"), m_portSpin);
    form->addRow(tr_("rview.token_label"), tokenRow);
    form->addRow(tr_("rview.url_label"), m_urlPreview);
    form->addRow(QString(), copyBtn);
    form->addRow(warningLabel);

    auto *saveBtn = new QPushButton(tr_("rview.save_btn"), this);
    saveBtn->setProperty("accent", true);

    connect(saveBtn, &QPushButton::clicked, this, &RemoteViewPage::onSave);
    connect(regenBtn, &QPushButton::clicked, this, &RemoteViewPage::onRegenerateToken);
    connect(copyBtn, &QPushButton::clicked, this, &RemoteViewPage::onCopyUrl);
    connect(m_portSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &RemoteViewPage::refreshUrlPreview);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch();
    btnRow->addWidget(saveBtn);

    auto *mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(heading);
    mainLayout->addSpacing(8);
    mainLayout->addLayout(form);
    mainLayout->addStretch();
    mainLayout->addLayout(btnRow);

    refreshUrlPreview();
}

void RemoteViewPage::refreshUrlPreview()
{
    QString url = QString("http://%1:%2/status?token=%3")
                      .arg(localIpAddress())
                      .arg(m_portSpin->value())
                      .arg(m_tokenEdit->text());

    m_urlPreview->setText(url);
}

void RemoteViewPage::onRegenerateToken()
{
    unsigned char randomBytes[16];
    HCRYPTPROV hProv = 0;
    char token[64] = {0};

    if (
        CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT) &&
        CryptGenRandom(hProv, sizeof(randomBytes), randomBytes)
    )
    {
        for (int i = 0; i < 16; i++)
            snprintf(token + i * 2, 3, "%02x", randomBytes[i]);

        CryptReleaseContext(hProv, 0);
    }

    m_tokenEdit->setText(QString::fromUtf8(token));
    refreshUrlPreview();
}

void RemoteViewPage::onCopyUrl()
{
    QApplication::clipboard()->setText(m_urlPreview->text());
    QMessageBox::information(this, "JustInTime", tr_("rview.copied"));
}

void RemoteViewPage::onSave()
{
    AppSettings s;
    settings_get(&s);

    s.remote_view_enabled = m_enabledCheck->isChecked() ? 1 : 0;
    s.remote_view_port    = m_portSpin->value();

    QByteArray tokenUtf8 = m_tokenEdit->text().toUtf8();
    snprintf(s.remote_view_token, sizeof(s.remote_view_token), "%s", tokenUtf8.constData());

    if (!settings_update(&s))
    {
        QMessageBox::warning(
            this, "JustInTime",
            QString("[%1] Failed to save settings.").arg(ERR_SETTINGS_SAVE_FAIL)
        );
        return;
    }

    /*
     * Áp dụng ngay: khởi động lại server với cấu hình mới
     * (bật/tắt, đổi port, đổi token) mà không cần restart app.
     */
    remoteview_restart();

    QMessageBox::information(this, "JustInTime", tr_("rview.saved"));
}
