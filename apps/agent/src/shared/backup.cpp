//
// backup.cpp
// Backup dữ liệu cục bộ ra file JSON, tách biệt hoàn toàn với việc
// đồng bộ lên Supabase, để đảm bảo luôn có ít nhất một bản sao dữ
// liệu an toàn ngay cả khi mất kết nối mạng dài ngày.
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface extern "C" trong
// backup.h). Thay đổi thật sự:
//   - std::vector<std::string> thay cho "char names[512][MAX_PATH]"
//     cấp phát tay trên stack (~133KB!) - bản C cũ giới hạn CỨNG 512
//     file, nếu vì lý do gì đó (cleanup không chạy 1 thời gian dài,
//     hoặc BACKUP_KEEP_COUNT tăng lên) có nhiều hơn 512 file backup
//     tồn tại, phần dư ra không được xét dọn dẹp. vector không có
//     giới hạn cứng này.
//   - std::sort thay cho insertion sort tự viết tay.
//   - RAII cho HANDLE tìm file (FindFirstFileA) - tự đóng khi ra
//     khỏi scope.
//
// Đợt tối ưu sau đó:
//   - Đường dẫn dùng std::filesystem (UTF-16), FindFirstFileA/FindHandle
//     được thay bằng directory_iterator - chạy đúng với tên user Unicode.
//   - Bỏ qua backup nếu DB không thay đổi kể từ lần backup trước.
//   - File JSON được ghi ra file tạm rồi đổi tên (xem db_export_json), nên
//     backup dở dang do crash không bao giờ chiếm 1 trong 14 chỗ giữ lại.
//

#include "backup.h"
#include "database.h"
#include "config.h"
#include "settings.h"
#include "paths.h"
#include "strutil.h"
#include "log.h"
#include "error_codes.h"

#include <windows.h>

#include <cstdio>
#include <cwchar>
#include <ctime>
#include <algorithm>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

namespace {

/*
 * Thư mục backup tuyệt đối (%APPDATA%\\JustInTime\\backups), tạo nếu chưa có.
 * QUAN TRỌNG: không dùng đường dẫn tương đối, vì khi app tự khởi động
 * cùng Windows (autostart), thư mục làm việc hiện tại có thể khác thư mục
 * chứa file .exe. Dùng std::filesystem (UTF-16) để chạy đúng cả khi tên
 * tài khoản Windows có ký tự Unicode.
 */
std::filesystem::path getBackupDir()
{
    const std::filesystem::path dir = jit::configFile(std::filesystem::path(BACKUP_DIR).c_str());
    if (dir.empty())
        return {};

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    return dir;
}

/*
 * Xóa bớt các file backup cũ, chỉ giữ lại BACKUP_KEEP_COUNT file gần
 * nhất. Vì tên file có định dạng backup_YYYYMMDD_HHMMSS.json nên sắp
 * xếp theo thứ tự chữ cái cũng chính là sắp xếp theo thời gian.
 */
void cleanupOldBackups(const std::filesystem::path& backupDir)
{
    std::vector<std::filesystem::path> files;

    std::error_code ec;
    for (std::filesystem::directory_iterator it(backupDir, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::filesystem::path& p = it->path();
        const std::wstring name = p.filename().wstring();

        if (name.rfind(L"backup_", 0) == 0 && p.extension() == L".json")
            files.push_back(p);
    }

    std::sort(files.begin(), files.end());

    const int toDelete = static_cast<int>(files.size()) - BACKUP_KEEP_COUNT;

    for (int i = 0; i < toDelete; i++)
        std::filesystem::remove(files[static_cast<size_t>(i)], ec);
}

// Số thay đổi DB tại lần backup gần nhất; -1 = chưa backup lần nào.
long long g_lastBackupChanges = -1;

} // namespace

int backup_create_snapshot(void)
{
    /*
     * Không có gì thay đổi kể từ lần backup trước => bỏ qua. Backup xuất
     * TOÀN BỘ bảng ra JSON (kể cả 30 ngày record đã sync), nên khi máy
     * rảnh/khoá màn hình suốt vài tiếng thì 14 file backup giữ lại chỉ
     * là 14 bản sao y hệt nhau - vừa tốn I/O vừa đẩy mất các bản backup
     * cũ có giá trị.
     */
    const long long changes = db_change_counter();
    if (g_lastBackupChanges >= 0 && changes == g_lastBackupChanges)
        return 1;

    const std::filesystem::path backupDir = getBackupDir();
    if (backupDir.empty())
        return 0;

    time_t now = time(NULL);
    struct tm t;
    localtime_s(&t, &now);

    wchar_t fileName[64];
    swprintf(
        fileName, 64,
        L"backup_%04d%02d%02d_%02d%02d%02d.json",
        t.tm_year + 1900, t.tm_mon + 1, t.tm_mday,
        t.tm_hour, t.tm_min, t.tm_sec
    );

    const std::filesystem::path filePath = backupDir / fileName;
    const std::string filePathUtf8 = jit::wideToUtf8(filePath.wstring());

    const int ok = db_export_json(filePathUtf8.c_str());

    if (ok)
    {
        g_lastBackupChanges = changes;
        JIT_LOG(L"[BACKUP] Da sao luu du lieu vao %ls\n", filePath.c_str());
        cleanupOldBackups(backupDir);
    }
    else
    {
        JIT_LOG(L"[BACKUP][%hs] Sao luu that bai\n", ERR_DB_EXPORT_FAIL);
    }

    return ok;
}
