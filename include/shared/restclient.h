//
// restclient.h
// Client HTTP tổng quát để gọi Supabase REST (PostgREST) hoặc
// RPC bằng access_token của session đang đăng nhập. Được tách
// ra dùng chung cho parentlink.cpp và applimits.cpp, thay vì mỗi
// module tự viết lại WinHTTP request (network.c vẫn giữ code
// riêng vì nó gọi Edge Function - không phải PostgREST/RPC
// trực tiếp - và không cần refactor gộp chung ở đây).
//

#ifndef RESTCLIENT_H
#define RESTCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif


#include <windows.h>

/*
 * Gọi 1 request bất kỳ tới Supabase REST/RPC, dùng access_token
 * của session hiện tại (phải đã đăng nhập trước - xem auth.h).
 *
 * method        : "GET", "POST", "PATCH", "DELETE".
 * path          : phải bắt đầu bằng "/", vd
 *                  "/rest/v1/parent_links?select=*&status=eq.pending".
 * body          : JSON string; NULL nếu không cần body (GET/DELETE).
 * extra_headers : chuỗi header bổ sung, mỗi dòng kết thúc bằng
 *                 "\r\n" (vd "Prefer: return=representation\r\n");
 *                 NULL nếu không cần thêm gì.
 * response_out  : nhận JSON trả về (dù thành công hay lỗi, để
 *                 caller tự đọc thông báo lỗi nếu cần).
 * status_out    : nhận mã HTTP.
 *
 * Trả về 1 nếu request được gửi/nhận phản hồi (bất kể status
 * HTTP là gì - caller tự kiểm tra *status_out), 0 nếu bản thân
 * request thất bại ở tầng WinHTTP (không kết nối được, v.v.)
 */
int restclient_call(
    const char* method,
    const wchar_t* path,
    const char* body,
    const wchar_t* extra_headers,
    char* response_out, int response_out_size,
    DWORD* status_out
);

/*
 * Gọi khi 1 request bị Supabase trả 401 (access_token hết hạn - mặc định
 * chỉ sống ~1 giờ). used_token là access_token đã dùng cho request thất bại.
 *
 *   - Nếu session hiện tại ĐÃ có token khác (1 luồng khác vừa refresh xong)
 *     thì không refresh lần nữa (refresh_token của Supabase xoay vòng, gọi
 *     đồng thời từ 2 luồng có thể làm 1 bên bị từ chối -> đăng xuất oan).
 *   - Ngược lại gọi auth_refresh_session().
 *
 * Trả về 1 nếu giờ đã có token mới để thử lại, 0 nếu không.
 * restclient_call() tự gọi hàm này; network.cpp cũng dùng chung.
 */
int restclient_refresh_if_stale(const char* used_token);


#ifdef __cplusplus
}
#endif

#endif
