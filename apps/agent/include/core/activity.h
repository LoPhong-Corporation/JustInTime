//
// Created by LoPhongCorporation on 6/24/2026.
//

#ifndef ACTIVITY_H
#define ACTIVITY_H

#ifdef __cplusplus
extern "C" {
#endif


#include <windows.h>
#include <time.h>

typedef struct
{
    wchar_t process_name[MAX_PATH];
    wchar_t window_title[512];
} ActiveWindow;

typedef struct
{
    wchar_t process_name[MAX_PATH];
    wchar_t window_title[512];

    time_t start_time;
    time_t end_time;

    long duration_seconds;

    int synced;
} ActivityRecord;

void monitor_activity(void);

/*
 * Gọi ngay khi phát hiện máy chuẩn bị khoá màn hình hoặc đi
 * ngủ (sleep/hibernate) - xem main.cpp, bắt qua
 * WM_WTSSESSION_CHANGE (WTS_SESSION_LOCK) và WM_POWERBROADCAST
 * (PBT_APMSUSPEND).
 *
 * Chốt sổ record đang mở NGAY TẠI THỜI ĐIỂM NÀY (không đợi
 * app đổi cửa sổ), rồi reset trạng thái theo dõi. Nhờ vậy,
 * khi máy mở khoá/thức dậy - dù cửa sổ active lúc đó có trùng
 * y hệt cửa sổ trước khi khoá - monitor_activity() vẫn luôn
 * bắt đầu MỘT RECORD HOÀN TOÀN MỚI, thay vì merge cả khoảng
 * thời gian khoá máy/ngủ vào thời lượng dùng app (đây chính là
 * nguyên nhân gây ra các record duration_seconds bất thường lớn
 * kiểu hàng chục phút/hàng giờ trong DB).
 */
void activity_suspend(void);

/*
 * Gọi khi máy mở khoá màn hình / thức dậy từ sleep (WM_WTSSESSION_CHANGE
 * với WTS_SESSION_UNLOCK, hoặc WM_POWERBROADCAST với
 * PBT_APMRESUMEAUTOMATIC/PBT_APMRESUMESUSPEND).
 *
 * Không bắt buộc phải gọi để logic hoạt động đúng (monitor_activity()
 * tự bắt đầu phiên mới nhờ activity_suspend() đã reset trạng thái),
 * nhưng vẫn expose ra để log rõ thời điểm resume và dự phòng mở
 * rộng sau này (vd ghi nhận "khoảng gap" vào DB để hiển thị).
 */
void activity_resume(void);

/*
 * Lý do 1 app bị chặn - dùng để agent (trayicon.cpp) hiển thị
 * thông báo đúng nội dung cho người dùng.
 */
#define LIMIT_REASON_BLOCKED    1  /* phụ huynh chặn hẳn app này */
#define LIMIT_REASON_TIME_UP    2  /* đã dùng đủ số giờ/ngày cho phép */

typedef struct
{
    wchar_t process_name[MAX_PATH];
    int      reason;
} ActivityLimitEvent;

/*
 * Kiểm tra & THỰC THI (tự chốt sổ + tự kill) giới hạn app do
 * phụ huynh đặt (bảng app_limits, xem applimits.h) - CHỈ có
 * tác dụng nếu máy này đang ở vai trò APP_ROLE_PARENT... à
 * không, ngược lại: chỉ chạy khi máy này là APP_ROLE_CHILD
 * và đã đăng nhập.
 *
 * Quan trọng về triết lý thiết kế: hàm này chỉ THỰC THI (kill
 * process cục bộ dựa trên "luật" đọc được từ Supabase), KHÔNG
 * có bất kỳ lệnh điều khiển trực tiếp nào từ phụ huynh - máy
 * con luôn là bên quyết định cuối cùng, và luôn báo lại (qua
 * events_out) để agent hiển thị thông báo minh bạch, không bao
 * giờ âm thầm.
 *
 * Gọi định kỳ (khuyến nghị mỗi 15-30s là đủ, không cần nhanh
 * như monitor_activity()). Trả về số lượng app vừa bị chặn
 * trong lần gọi này (0 nếu không có gì xảy ra), điền tối đa
 * max_events phần tử vào events_out.
 */
int activity_check_limits(
    ActivityLimitEvent* events_out,
    int max_events
);

/*
 * Lấy snapshot activity đang diễn ra ngay lúc này
 * (process/title/thời điểm bắt đầu), thread-safe.
 * Dùng cho remote view - xem real-time không qua cloud.
 */
void activity_get_current(
    wchar_t* process_out, int process_size,
    wchar_t* title_out, int title_size,
    time_t* since_out
);


#ifdef __cplusplus
}
#endif

#endif