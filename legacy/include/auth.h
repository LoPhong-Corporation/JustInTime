//
// auth.h
// Đăng nhập / đăng ký qua Supabase Auth (GoTrue REST API),
// lưu session cục bộ, mã hóa bằng Windows DPAPI (gắn với
// tài khoản Windows hiện tại của bạn, giống cách trình
// duyệt lưu mật khẩu đã lưu).
//

#ifndef AUTH_H
#define AUTH_H

#ifdef __cplusplus
extern "C" {
#endif


#define MAX_TOKEN_LEN 4096
#define MAX_EMAIL_LEN  256

typedef struct
{
    int  logged_in;

    char user_id[64];
    char email[MAX_EMAIL_LEN];

    char access_token[MAX_TOKEN_LEN];
    char refresh_token[512];

} AuthSession;

/*
 * Đăng ký tài khoản mới.
 * Trả về 1 nếu thành công, 0 nếu thất bại
 * (err_out sẽ chứa lý do, lấy từ Supabase).
 */
int auth_register(
    const char* email,
    const char* password,
    char* err_out,
    int err_out_size
);

/*
 * Đăng nhập. Nếu thành công, session được lưu lại
 * (mã hóa) và có thể lấy qua auth_get_session().
 */
int auth_login(
    const char* email,
    const char* password,
    char* err_out,
    int err_out_size
);

/*
 * Đăng xuất: xóa session khỏi bộ nhớ và file lưu.
 */
void auth_logout(void);

/*
 * Dùng refresh_token đã lưu (từ lần đăng nhập gần nhất) để
 * xin access_token mới, khi access_token hiện tại hết hạn
 * hoặc bị Supabase từ chối (HTTP 401, vd "Invalid JWT").
 *
 * Nếu thành công: session trong bộ nhớ VÀ session.dat trên
 * đĩa đều được cập nhật với access_token/refresh_token mới,
 * y hệt sau một lần đăng nhập bình thường.
 *
 * Nếu thất bại (refresh_token cũng đã hết hạn/bị thu hồi,
 * hoặc project đã đổi JWT signing key khiến toàn bộ token
 * cũ - kể cả refresh_token - không còn hợp lệ): trả về 0,
 * và người dùng cần đăng nhập lại thủ công qua tray.
 *
 * Trả về 1 nếu refresh thành công, 0 nếu thất bại.
 */
int auth_refresh_session(void);

/*
 * Nạp lại session đã lưu từ trước (lúc khởi động app).
 * Trả về 1 nếu có session hợp lệ, 0 nếu chưa đăng nhập.
 */
int auth_load_session(void);

/*
 * Lấy bản sao session hiện tại (thread-safe).
 */
void auth_get_session(AuthSession* out);

/*
 * Có đang đăng nhập không.
 */
int auth_is_logged_in(void);


#ifdef __cplusplus
}
#endif

#endif
