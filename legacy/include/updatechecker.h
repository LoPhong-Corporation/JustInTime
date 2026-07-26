//
// updatechecker.h
// Kiểm tra phiên bản mới qua bảng công khai "app_releases" trên
// Supabase (chỉ đọc, không cần đăng nhập - RLS cho phép anon
// SELECT, xem migrations/003_app_releases.sql).
//
// Thiết kế có chủ đích: KHÔNG tự tải và tự thay thế file .exe
// đang chạy. Chỉ báo cho người dùng biết có bản mới + đưa link
// tải về, người dùng luôn là người quyết định khi nào cài đặt -
// an toàn hơn nhiều so với một cơ chế tự ghi đè binary.
//

#ifndef UPDATECHECKER_H
#define UPDATECHECKER_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

class UpdateChecker : public QObject
{
    Q_OBJECT

public:
    explicit UpdateChecker(QObject *parent = nullptr);

    /*
     * Bắt đầu 1 lần kiểm tra (bất đồng bộ - kết quả trả về qua
     * các signal bên dưới, không block UI thread).
     */
    void checkNow();

signals:
    void updateAvailable(const QString &version, const QString &downloadUrl, const QString &notes);
    void upToDate();
    void checkFailed(const QString &reason);

private slots:
    void onReply(QNetworkReply *reply);

private:
    QNetworkAccessManager *m_manager;
};

#endif
