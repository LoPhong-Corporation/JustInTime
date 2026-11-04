//
// i18n.h
// Cổng từ internal/i18n/i18n.go. Dữ liệu dịch (168 khoá x 2 ngôn ngữ,
// đã đối chiếu khớp 100% với bản Go - xem web/i18n.json) được TÁCH RA
// dữ liệu JSON thay vì gõ tay lại thành các std::map C++, để không có
// nguy cơ gõ sai/thiếu 1 khoá nào trong quá trình cổng hoá.
//
#ifndef DASHBOARD_I18N_H
#define DASHBOARD_I18N_H

#include <nlohmann/json.hpp>
#include <string>

namespace jit::dash {

// Nạp web/i18n.json (đường dẫn cạnh file .exe hoặc theo webRoot truyền vào).
// Ném std::runtime_error nếu không đọc/parse được - đây là dữ liệu bắt
// buộc phải có để dashboard hoạt động, không có fallback hợp lý.
void loadI18n(const std::string& webRootUtf8);

// Bảng dịch cho 1 ngôn ngữ, mặc định về "en" nếu không có - khớp hành vi
// i18n.Dict() bên Go.
const nlohmann::json& i18nDict(const std::string& language);

} // namespace jit::dash

#endif
