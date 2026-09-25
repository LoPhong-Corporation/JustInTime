//
// log.h
// Log debug có "cổng" bật/tắt. Trước đây MỌI wprintf() trong code lõi luôn
// chạy (định dạng chuỗi + ghi ra 1 console ẩn) kể cả khi không ai xem, và
// console được tạo sẵn ngay lúc khởi động (nháy 1 cửa sổ đen trước khi ẩn).
//
// Giờ: JIT_LOG() không làm gì cả (không format, không I/O) cho tới khi
// người dùng bật "Debug console" trong tray - lúc đó console mới được tạo
// (lười), và nút [X] của nó bị vô hiệu hoá để bấm nhầm không tắt cả app.
//

#ifndef LOG_H
#define LOG_H

#include <wchar.h>

#ifdef __cplusplus
extern "C" {
#endif

/* 1 nếu đang bật log (console debug đang hiện). Rẻ: 1 lần đọc atomic. */
int jit_log_enabled(void);

/*
 * Hiện/ẩn console debug. Lần hiện đầu tiên sẽ tạo console. Chỉ gọi từ
 * GUI thread (tray menu).
 */
void jit_console_show(int visible);

#ifdef __cplusplus
}
#endif

#define JIT_LOG(...)                          \
    do {                                      \
        if (jit_log_enabled())                \
            wprintf(__VA_ARGS__);             \
    } while (0)

#endif
