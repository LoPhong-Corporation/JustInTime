//
// machines.h
//
// Trước đây CHỈ dashboard-go (Go) đẩy heartbeat lên bảng
// device_heartbeats - agent C không bao giờ tự làm việc này. Hệ quả:
// nếu người dùng không bao giờ mở dashboard web, "last_seen" của máy
// này đứng yên mãi mãi, và các máy khác luôn thấy nó "Offline" dù
// tray agent (luôn chạy nền) vẫn đang theo dõi bình thường - đây
// chính là lý do "tính năng đồng bộ không hoạt động". File này để
// agent C (luôn chạy) tự đẩy heartbeat của MÌNH định kỳ, không phụ
// thuộc dashboard web có đang mở hay không, và đọc lại toàn bộ danh
// sách máy để hiển thị trong ControlPanelWindow (Overview) - giống
// hệt mục "Machines" bên dashboard Go.
//

#ifndef MACHINES_H
#define MACHINES_H

#ifdef __cplusplus
extern "C" {
#endif

#include <windows.h>

typedef struct {
    char device_id[64];
    char hostname[256];  // nhãn hiển thị (đã ẩn danh - xem device.h)
    double cpu_percent;
    double ram_percent;
    double disk_percent;
    char last_seen[64];  // ISO 8601 UTC, y hệt định dạng Supabase trả về
} MachineHeartbeat;

#define MACHINES_MAX 50

/*
 * Đẩy heartbeat của CHÍNH máy này lên Supabase (upsert theo
 * device_id) - KHÔNG kèm số liệu CPU/RAM/Disk thật (agent C chưa có
 * sẵn hạ tầng lấy số liệu này; dashboard Go vẫn là nguồn số liệu
 * chi tiết khi nó chạy) - mục đích chính là giữ "last_seen" luôn
 * mới, để máy này không bị các máy khác thấy nhầm là "Offline" chỉ
 * vì không ai mở dashboard web. Yêu cầu đã đăng nhập; im lặng bỏ
 * qua (trả về 0) nếu chưa đăng nhập hoặc mất mạng - không phải lỗi
 * nghiêm trọng, thử lại ở lần gọi định kỳ tiếp theo.
 */
int machines_push_heartbeat(void);

/*
 * Lấy danh sách mọi máy trên tài khoản (bảng device_heartbeats) -
 * dùng cho phần "Machines" trong ControlPanelWindow Overview.
 * Trả về số máy lấy được (0 nếu chưa đăng nhập/lỗi mạng/chưa có
 * máy nào).
 */
int machines_list(
    MachineHeartbeat* out,
    int max_entries
);

#ifdef __cplusplus
}
#endif

#endif
