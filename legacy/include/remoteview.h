//
// remoteview.h
// HTTP server cục bộ CHỈ ĐỌC, cho phép xem số liệu
// real-time trực tiếp từ agent, KHÔNG qua cloud, KHÔNG
// có bất kỳ endpoint nào thay đổi/điều khiển được gì.
// Tắt mặc định, bảo vệ bằng token ngẫu nhiên.
//

#ifndef REMOTEVIEW_H
#define REMOTEVIEW_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Khởi động server nếu remote_view_enabled=1 trong settings.
 * Không làm gì (trả về 1) nếu đang tắt. Chạy trên thread
 * riêng, không block caller.
 * Trả về 1 nếu ok, 0 nếu lỗi thực sự (bind/socket thất bại).
 */
int remoteview_start(void);

/*
 * Dừng server (gọi lúc thoát app, hoặc khi người dùng tắt
 * tính năng trong Settings để áp dụng ngay).
 */
void remoteview_stop(void);

/*
 * Khởi động lại theo cấu hình mới nhất (gọi sau khi lưu
 * Settings, để bật/tắt hoặc đổi port có hiệu lực ngay
 * không cần khởi động lại app).
 */
int remoteview_restart(void);

#ifdef __cplusplus
}
#endif

#endif
