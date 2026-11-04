//
// main.cpp
//
// Điểm vào CỦA RIÊNG dashboard.exe (lại tách khỏi JustInTime.exe - xem
// ghi chú ở README cấp repo về quyết định tách/gộp). Qt Widgets app
// thuần, không HTTP server, không trình duyệt.
//
// Single-instance: trước đây (khi còn là HTTP server) "đã chạy chưa" được
// biết bằng cách thử connect cổng 5000. Giờ không còn cổng nào - dùng 1
// named mutex Win32 làm cờ "đang chạy": nếu mutex đã tồn tại (tiến trình
// dashboard.exe khác đang sống), tiến trình mới chỉ việc tìm cửa sổ của
// tiến trình cũ (theo tên lớp cửa sổ) rồi đưa lên trước, không dựng UI
// thứ hai.
//
#include "dashconfig.h"
#include "localdb.h"
#include "ui/mainwindow.h"
#include "ui/theme.h"

#include <windows.h>

#include <QApplication>
#include <QIcon>

#include <memory>

namespace {

constexpr wchar_t kSingleInstanceMutexName[] = L"Local\\JustInTimeDashboard_SingleInstance";
// Qt đặt tên lớp cửa sổ kiểu "QWidgetClassWindow" không ổn định giữa các
// bản Qt khác nhau - dùng windowTitle() cố định (đặt dưới đây) để FindWindowW
// tìm đúng cửa sổ, đáng tin cậy hơn dò theo class name.
constexpr wchar_t kWindowTitle[] = L"JustInTime - Thời Gian Máy";

} // namespace

int main(int argc, char* argv[])
{
    // Giữ handle sống tới hết main() - nhả ra (CloseHandle tự động khi
    // process thoát) là lúc "đã chạy" hết hiệu lực, không cần dọn tay.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, kSingleInstanceMutexName);
    const bool alreadyRunning = (GetLastError() == ERROR_ALREADY_EXISTS);

    if (alreadyRunning)
    {
        if (HWND existing = FindWindowW(nullptr, kWindowTitle))
        {
            ShowWindow(existing, SW_RESTORE);
            SetForegroundWindow(existing);
        }
        if (mutex)
            CloseHandle(mutex);
        return 0;
    }

    QApplication app(argc, argv);
    app.setApplicationName("JustInTime");
    app.setApplicationDisplayName(QString::fromWCharArray(kWindowTitle));
    app.setWindowIcon(QIcon(":/branding/icon.ico"));

    jit::dash::Config cfg = jit::dash::load();
    auto db = jit::dash::LocalDB::open(cfg.localDbPathUtf8);
    // db == nullptr là bình thường lúc mới cài (agent chưa tạo DB lần nào) -
    // các trang liên quan tự hiện thông báo, không phải crash.

    auto* window = new jit::dash::ui::MainWindow(cfg, std::move(db));
    // QString không chắc có constructor ngầm nhận const wchar_t* trên mọi
    // bản Qt/compiler - dùng fromWCharArray() cho chắc thay vì trông chờ
    // chuyển đổi ngầm.
    window->setWindowTitle(QString::fromWCharArray(kWindowTitle));
    window->setStyleSheet(jit::dash::ui::darkThemeStyleSheet());
    window->show();

    const int ret = app.exec();
    if (mutex)
        CloseHandle(mutex);
    return ret;
}
