//
// httpclient.h
// HTTPS client tối giản trên WinHTTP, dùng chung cho restclient.cpp và
// network.cpp (trước đây mỗi lời gọi HTTP tự WinHttpOpen + WinHttpConnect
// + bắt tay TLS lại từ đầu, và 2 file có 2 bản copy gần như y hệt của
// cùng 1 đoạn code ~90 dòng).
//
// Khác biệt chính:
//   - Giữ session + connection sống lâu (mỗi THREAD 1 bộ, không cần khoá):
//     các request liên tiếp tới cùng host tái sử dụng kết nối TCP/TLS
//     (keep-alive) thay vì bắt tay TLS ~100-300ms mỗi lần.
//   - Có timeout (resolve/connect/send/receive). Trước đây không đặt gì,
//     nên 1 lần mất mạng "nửa vời" có thể treo worker thread hàng chục
//     giây - trong lúc đó việc theo dõi hoạt động cũng đứng.
//   - Header/body/response dùng std::string/std::wstring, không còn
//     buffer 4096 ký tự cố định cho header (access_token dài ~1-2KB +
//     apikey dễ làm tràn/cắt cụt).
//

#ifndef HTTPCLIENT_H
#define HTTPCLIENT_H

#include <windows.h>

#include <string>

namespace jit {

struct HttpResult
{
    DWORD status = 0;
    std::string body;
};

/*
 * Gửi 1 request HTTPS tới `host` (chỉ tên miền, KHÔNG có "https://").
 *
 * extraHeaders: các dòng "Name: value\r\n" (đã gồm Content-Type nếu cần).
 * idempotent  : true nếu gửi lại an toàn (GET, upsert...) - khi lỗi tầng
 *               giao vận (kết nối keep-alive đã bị server đóng...), hàm tự
 *               dựng lại kết nối và thử thêm 1 lần.
 *
 * Trả về true nếu nhận được phản hồi HTTP (bất kể status), false nếu lỗi
 * tầng giao vận (không kết nối được, timeout...).
 */
bool httpsRequest(
    const std::wstring& host,
    const wchar_t* method,
    const std::wstring& path,
    const std::wstring& extraHeaders,
    const std::string& body,
    HttpResult& out,
    bool idempotent,
    size_t maxBody = 4u << 20);

} // namespace jit

#endif
