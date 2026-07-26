//
// tray.h
// Icon khay hệ thống (system tray) + menu ngữ cảnh:
// Đăng nhập/Đăng xuất, Tạm dừng, Báo cáo, Cài đặt,
// Setup Supabase, Hiện/Ẩn console debug, Thoát.
//

#ifndef TRAY_H
#define TRAY_H

#include <windows.h>

/*
 * Khởi tạo tray icon + cửa sổ ẩn xử lý message.
 * Gọi 1 lần trong WinMain trước khi vào message loop.
 * Trả về HWND cửa sổ ẩn, hoặc NULL nếu lỗi.
 */
HWND tray_init(HINSTANCE hInstance);

/*
 * Có đang tạm dừng theo dõi hay không (điều khiển qua
 * menu tray "Tạm dừng theo dõi"). Worker thread kiểm tra
 * hàm này trước mỗi lần gọi monitor_activity().
 */
int tray_is_paused(void);

/*
 * Dọn dẹp tray icon (gọi trước khi thoát app).
 */
void tray_cleanup(void);

#endif
