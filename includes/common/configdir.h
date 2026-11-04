//
// configdir.h
// Đường dẫn thư mục cấu hình chung %APPDATA%\JustInTime - KHÔNG phụ thuộc
// AppSettings hay bất kỳ thứ gì riêng của agent. Tách riêng ra khỏi
// settings.cpp (vốn chủ yếu là cấu hình RIÊNG của agent - sync interval,
// backup interval...) để libs/common không kéo theo baggage của agent khi
// apps/dashboard chỉ cần đúng 1 việc: biết %APPDATA%\JustInTime nằm ở đâu.
// paths.h (jit::configFile) dùng hàm này làm nền.
//
#ifndef JIT_CONFIGDIR_H
#define JIT_CONFIGDIR_H

namespace jit {

// Ghi "%APPDATA%\JustInTime" (UTF-16) vào out (tối đa outChars ký tự, kể cả
// '\0'), tạo thư mục nếu chưa có. Trả về 1 nếu thành công, 0 nếu thất bại
// (APPDATA không đọc được, hoặc buffer quá nhỏ).
int configDirW(wchar_t* out, int outChars);

} // namespace jit

#endif
