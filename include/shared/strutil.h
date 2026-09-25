//
// strutil.h
// Tiện ích chuỗi dùng chung (header-only, C++ thuần): UTF-8 <-> UTF-16
// và escape JSON. Trước đây mỗi file .cpp (network, applimits, auth,
// machines, parentlink, remoteview...) tự chép lại 1 bản riêng của
// các hàm này. Các file mới/được viết lại dùng bản chung này; các file
// cũ có thể chuyển dần sang (chỉ cần xoá bản local và thêm
// `using jit::jsonEscape;`).
//

#ifndef STRUTIL_H
#define STRUTIL_H

#include <windows.h>

#include <cstdio>
#include <string>
#include <string_view>

namespace jit {

inline std::wstring utf8ToWide(std::string_view s)
{
    if (s.empty())
        return {};

    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (n <= 0)
        return {};

    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

inline std::string wideToUtf8(std::wstring_view w)
{
    if (w.empty())
        return {};

    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    if (n <= 0)
        return {};

    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

/*
 * Escape 1 chuỗi UTF-8 để đặt vào giữa cặp ngoặc kép của JSON. Không
 * có giới hạn độ dài cố định (khác json_escape() kiểu C, vốn cắt cụt
 * âm thầm nếu buffer thiếu) - dùng std::string tự lớn theo nhu cầu.
 */
inline std::string jsonEscape(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + s.size() / 8 + 8);

    for (const unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else
                {
                    out += static_cast<char>(c);
                }
        }
    }

    return out;
}

/* "https://abc.supabase.co/" -> "abc.supabase.co" (bỏ scheme + phần path). */
inline std::string hostFromUrl(std::string_view url)
{
    const size_t scheme = url.find("://");
    if (scheme != std::string_view::npos)
        url.remove_prefix(scheme + 3);

    const size_t slash = url.find('/');
    if (slash != std::string_view::npos)
        url = url.substr(0, slash);

    return std::string(url);
}

} // namespace jit

#endif
