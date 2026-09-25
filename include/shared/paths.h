//
// paths.h
// Đường dẫn file cấu hình/dữ liệu dạng UTF-16 + ghi file "atomic".
//
// Vì sao: trước đây thư mục cấu hình lấy từ getenv("APPDATA") - chuỗi theo
// ANSI code page, không phải UTF-8 - rồi truyền thẳng cho sqlite3_open()
// (yêu cầu UTF-8) và các API ANSI (fopen/CreateDirectoryA...). Nếu tên
// tài khoản Windows có ký tự có dấu/Unicode (rất phổ biến với tên tiếng
// Việt) thì đường dẫn bị sai/không mở được và app báo lỗi "Failed to
// initialize database" ngay khi khởi động. Giờ mọi đường dẫn đi qua
// std::filesystem::path (UTF-16 trên Windows).
//
// writeFileAtomic: ghi ra file tạm rồi MoveFileEx đè lên file thật, để mất
// điện/crash giữa chừng không bao giờ để lại settings.ini/session.dat/
// device.id bị cắt cụt (trước đây ghi thẳng bằng ofstream + trunc).
//

#ifndef PATHS_H
#define PATHS_H

#include "settings.h"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace jit {

/* %APPDATA%\JustInTime\<name>; path rỗng nếu không lấy được thư mục. */
inline std::filesystem::path configFile(const wchar_t* name)
{
    wchar_t dir[MAX_PATH] = {0};

    if (!settings_get_config_dir_w(dir, MAX_PATH))
        return {};

    return std::filesystem::path(dir) / name;
}

inline bool writeFileAtomic(const std::filesystem::path& target, std::string_view bytes)
{
    if (target.empty())
        return false;

    std::filesystem::path tmp = target;
    tmp += L".tmp";

    {
        std::ofstream f(tmp, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!f)
            return false;

        f.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        f.flush();

        if (!f.good())
        {
            f.close();
            DeleteFileW(tmp.c_str());
            return false;
        }
    }

    if (!MoveFileExW(tmp.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DeleteFileW(tmp.c_str());
        return false;
    }

    return true;
}

} // namespace jit

#endif
