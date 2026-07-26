//
// updatechecker.cpp
//

#include "updatechecker.h"
#include "config.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrl>
#include <QUrlQuery>
#include <QtGlobal>

namespace {

/*
 * So sánh 2 chuỗi phiên bản dạng "MAJOR.MINOR.PATCH" (bỏ qua
 * tiền tố "v"/"V" nếu có, vd "v1.2.0"). Trả về giá trị dương
 * nếu a mới hơn b, âm nếu a cũ hơn b, 0 nếu bằng nhau.
 *
 * Không hiểu hậu tố kiểu "-beta"/"-rc1" (phần chữ bị bỏ qua,
 * coi như 0) - đủ dùng cho versioning kiểu semver cơ bản.
 */
int compareVersions(QString a, QString b)
{
    if (a.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        a.remove(0, 1);

    if (b.startsWith(QLatin1Char('v'), Qt::CaseInsensitive))
        b.remove(0, 1);

    const QStringList partsA = a.split(QLatin1Char('.'));
    const QStringList partsB = b.split(QLatin1Char('.'));

    const int count = qMax(partsA.size(), partsB.size());

    for (int i = 0; i < count; ++i)
    {
        bool okA = false;
        bool okB = false;

        const int va = i < partsA.size() ? partsA.at(i).toInt(&okA) : 0;
        const int vb = i < partsB.size() ? partsB.at(i).toInt(&okB) : 0;

        if (va != vb)
            return va - vb;
    }

    return 0;
}

} // namespace

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_manager(new QNetworkAccessManager(this))
{
    connect(m_manager, &QNetworkAccessManager::finished, this, &UpdateChecker::onReply);
}

void UpdateChecker::checkNow()
{
    QUrl url(QString(SUPABASE_URL) + QStringLiteral("/rest/v1/app_releases"));

    QUrlQuery query;
    query.addQueryItem(QStringLiteral("select"), QStringLiteral("version,download_url,notes"));
    query.addQueryItem(QStringLiteral("order"), QStringLiteral("published_at.desc"));
    query.addQueryItem(QStringLiteral("limit"), QStringLiteral("1"));
    url.setQuery(query);

    QNetworkRequest request(url);
    request.setRawHeader("apikey", SUPABASE_ANON_KEY);
    request.setRawHeader("Accept", "application/json");

    m_manager->get(request);
}

void UpdateChecker::onReply(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->error() != QNetworkReply::NoError)
    {
        emit checkFailed(reply->errorString());
        return;
    }

    const QByteArray data = reply->readAll();
    const QJsonDocument doc = QJsonDocument::fromJson(data);

    if (!doc.isArray() || doc.array().isEmpty())
    {
        emit checkFailed(QStringLiteral(
            "Unexpected response from update server (is migrations/003_app_releases.sql "
            "set up, and does it have at least one row?)"
        ));
        return;
    }

    const QJsonObject obj = doc.array().first().toObject();

    const QString latestVersion = obj.value(QStringLiteral("version")).toString();
    const QString downloadUrl   = obj.value(QStringLiteral("download_url")).toString();
    const QString notes         = obj.value(QStringLiteral("notes")).toString();

    if (latestVersion.isEmpty())
    {
        emit checkFailed(QStringLiteral("Update server returned no version info."));
        return;
    }

    if (compareVersions(latestVersion, QStringLiteral(APP_VERSION)) > 0)
        emit updateAvailable(latestVersion, downloadUrl, notes);
    else
        emit upToDate();
}
