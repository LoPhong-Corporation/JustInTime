//
// asyncutil.h
// Chạy 1 hàm có thể chặn (mọi lời gọi cloud::Client - WinHTTP đồng bộ) trên
// 1 thread nền, rồi gọi callback trên GUI thread khi xong. KHÔNG dùng
// QtConcurrent (tránh thêm 1 module Qt mới ngoài Widgets/Core/Gui đã có) -
// chỉ std::thread (detach) + QMetaObject::invokeMethod(..., Qt::QueuedConnection)
// để "nhảy" về GUI thread.
//
// QUAN TRỌNG (2 điều đã xác minh bằng test thật, xem BAO_CAO_UI_CPP.md):
//   1. KHÔNG bao giờ dùng macro `qApp` bên trong đoạn code chạy do 1 lệnh
//      invoke xuyên luồng gọi tới - luôn invoke THẲNG lên `context` (đối
//      tượng gọi hàm này), không phải lên qApp. Việc này còn có lợi phụ:
//      Qt tự động không phát sự kiện đã xếp hàng cho 1 QObject đã bị huỷ,
//      nên bản thân `context` vừa là đích invoke vừa là "hàng rào an toàn"
//      miễn phí.
//   2. KHÔNG gọi runAsync()/runAsyncVoid() trước khi vòng lặp sự kiện
//      (app.exec()) bắt đầu chạy - vd không gọi thẳng trong constructor
//      của 1 trang/dialog. Nếu trang cần tự tải dữ liệu lúc khởi tạo, hoãn
//      lệnh gọi đầu tiên bằng QTimer::singleShot(0, this, [this]{ ... }).
//
// `context`: QObject của trang/dialog gọi hàm này (thường `this`). Dùng
// QPointer để phát hiện nếu context đã bị huỷ (đóng dialog, chuyển trang)
// trong lúc request đang chạy - callback sẽ KHÔNG được gọi trong trường
// hợp đó, tránh chạm vào widget đã giải phóng.
//
#ifndef UI_ASYNCUTIL_H
#define UI_ASYNCUTIL_H

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include <exception>
#include <memory>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>

namespace jit::dash::ui {

// fn: () -> T, chạy trên thread nền, có thể ném exception.
// then: (T*, const std::exception*) - CHỈ MỘT trong hai khác nullptr.
//       Chạy trên GUI thread. result==nullptr nghĩa là fn() đã ném.
template <class Fn, class Then>
void runAsync(QObject* context, Fn&& fn, Then&& then)
{
    using T = std::invoke_result_t<Fn>;
    QPointer<QObject> guard(context);

    std::thread([fn = std::forward<Fn>(fn), then = std::forward<Then>(then), context, guard]() mutable {
        auto result = std::make_shared<T>();
        std::exception_ptr ep;
        try { *result = fn(); }
        catch (...) { ep = std::current_exception(); }

        // Invoke lên `context`, KHÔNG lên qApp (xem chú thích đầu file).
        QMetaObject::invokeMethod(context, [then = std::move(then), result, ep, guard]() mutable {
            if (!guard)
                return; // page/dialog đã đóng trong lúc chờ mạng - bỏ qua kết quả

            if (ep)
            {
                try { std::rethrow_exception(ep); }
                catch (const std::exception& e) { then(nullptr, &e); }
                catch (...) { std::runtime_error e("unknown error"); then(nullptr, &e); }
            }
            else
            {
                then(result.get(), nullptr);
            }
        }, Qt::QueuedConnection);
    }).detach();
}

// Phiên bản không có kết quả (chỉ cần biết thành công/lỗi) - vd gửi tin nhắn,
// duyệt lời mời, xoá giới hạn app.
template <class Fn, class Then>
void runAsyncVoid(QObject* context, Fn&& fn, Then&& then)
{
    QPointer<QObject> guard(context);

    std::thread([fn = std::forward<Fn>(fn), then = std::forward<Then>(then), context, guard]() mutable {
        std::exception_ptr ep;
        try { fn(); }
        catch (...) { ep = std::current_exception(); }

        QMetaObject::invokeMethod(context, [then = std::move(then), ep, guard]() mutable {
            if (!guard)
                return;

            if (ep)
            {
                try { std::rethrow_exception(ep); }
                catch (const std::exception& e) { then(&e); }
                catch (...) { std::runtime_error e("unknown error"); then(&e); }
            }
            else
            {
                then(nullptr);
            }
        }, Qt::QueuedConnection);
    }).detach();
}

} // namespace jit::dash::ui

#endif
