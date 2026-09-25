//
// applimits.h
//
// Giới hạn thời gian dùng app / chặn app, do phụ huynh đặt cho
// 1 tài khoản con CỤ THỂ - CHỈ áp dụng được nếu đã có liên kết
// parent_links ở trạng thái "approved" (thực thi bằng RLS, xem
// migrations/004_parent_links.sql).
//
// Quan trọng: agent (máy con) chỉ ĐỌC bảng này rồi TỰ quyết
// định chặn app - không có đường nào để phụ huynh điều khiển
// trực tiếp máy con (không remote command, không remote kill).
// Máy con luôn hiển thị rõ khi 1 app bị chặn (xem trayicon.cpp),
// không bao giờ âm thầm.
//

#ifndef APPLIMITS_H
#define APPLIMITS_H

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_LIMITS         64
#define MAX_LIMIT_PROCESS 260

typedef struct
{
    long id;
    char process_name[MAX_LIMIT_PROCESS];

    /*
     * Giây/ngày được phép dùng app này. -1 nghĩa là KHÔNG giới
     * hạn thời gian (chỉ có ý nghĩa nếu blocked cũng đang là 0 -
     * nếu không thì limit vô nghĩa vì app đã bị chặn hẳn).
     */
    int daily_limit_sec;

    /*
     * 1 = chặn hẳn app này, không cho chạy chút nào (bất kể
     * daily_limit_sec).
     */
    int blocked;
} AppLimit;

/*
 * (Vai trò phụ huynh) Lấy danh sách giới hạn đã đặt cho 1 con
 * (theo child_user_id - lấy từ ParentLink.other_user_id, chỉ
 * gọi được nếu link đó đã "approved", RLS sẽ tự chặn nếu chưa).
 */
int applimits_list_for_child(
    const char* child_user_id,
    AppLimit* out, int max_out
);

/*
 * (Vai trò phụ huynh) Đặt/cập nhật giới hạn cho 1 app cụ thể
 * của 1 con (upsert theo process_name - gọi lại với cùng tên
 * app sẽ ghi đè giới hạn cũ).
 * daily_limit_sec = -1 nghĩa là không giới hạn thời gian.
 */
int applimits_set(
    const char* child_user_id,
    const char* process_name,
    int daily_limit_sec,
    int blocked,
    char* err_out, int err_out_size
);

/*
 * (Vai trò phụ huynh) Xoá hẳn 1 giới hạn (không còn áp dụng gì
 * cho app đó nữa).
 */
int applimits_delete(
    long limit_id,
    char* err_out, int err_out_size
);

/*
 * (Vai trò con/agent) Lấy giới hạn của CHÍNH MÌNH từ BỘ NHỚ ĐỆM cục bộ -
 * dùng để tự kiểm tra và tự chặn app khi vượt ngưỡng (xem
 * activity_check_limits() trong core/activity.cpp). KHÔNG gọi mạng, rẻ
 * và an toàn để gọi từ luồng theo dõi.
 *
 * Trước đây hàm này gọi thẳng Supabase (1 kết nối TLS mới) mỗi 20 giây
 * NGAY TRÊN luồng theo dõi, và trả về "không có giới hạn nào" nếu mất
 * mạng/token hết hạn - tức là ngắt wifi (hoặc chờ access_token hết hạn)
 * là cách vô hiệu hoá toàn bộ giới hạn. Giờ danh sách được làm mới nền
 * bằng applimits_refresh_my_limits() và lưu ra đĩa, nên vẫn áp dụng được
 * khi offline hoặc sau khi khởi động lại mà chưa có mạng.
 */
int applimits_get_my_limits(
    AppLimit* out, int max_out
);

/*
 * Tải danh sách giới hạn mới nhất từ Supabase và cập nhật bộ nhớ đệm
 * (+ file limits.cache). Có chặn theo mạng - chỉ gọi từ luồng cloud.
 * Nếu lỗi (mất mạng, HTTP != 2xx) giữ nguyên bộ nhớ đệm cũ.
 * Trả về 1 nếu cập nhật được, 0 nếu không.
 */
int applimits_refresh_my_limits(void);


#ifdef __cplusplus
}
#endif

#endif
