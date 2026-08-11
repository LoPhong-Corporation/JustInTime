//
// backup.cpp
// Backup dữ liệu cục bộ ra file JSON, tách biệt hoàn toàn với việc
// đồng bộ lên Supabase, để đảm bảo luôn có ít nhất một bản sao dữ
// liệu an toàn ngay cả khi mất kết nối mạng dài ngày.
//
// CHUYỂN TỪ C SANG C++ (bản EXPERIMENTAL) - giữ nguyên interface extern "C" trong
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

#include "backup.h"
#include "database.h"
#include "config.h"
#include "settings.h"
#include "error_codes.h"

#include <windows.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>
#include <algorithm>

namespace {

// RAII cho HANDLE của FindFirstFileA/FindNextFileA.
class FindHandle
{
public:
    explicit FindHandle(HANDLE h) : m_handle(h) {}
    ~FindHandle()
    {
        if (m_handle != INVALID_HANDLE_VALUE)
            FindClose(m_handle);
    }
    FindHandle(const FindHandle&) = delete;
    FindHandle& operator=(const FindHandle&) = delete;

    bool valid() const { return m_handle != INVALID_HANDLE_VALUE; }
    HANDLE get() const { return m_handle; }

private:
    HANDLE m_handle;
};

/*
 * Lấy đường dẫn tuyệt đối tới thư mục backup
 * (%APPDATA%\JustInTime\backups), tạo nếu chưa có.
 * QUAN TRỌNG: không dùng đường dẫn tương đối, vì khi
 * app tự khởi động cùng Windows (autostart), thư mục
 * làm việc hiện tại có thể khác thư mục chứa file .exe.
 */
std::string getBackupDir()
{
    char configDir[MAX_PATH] = {0};
    settings_get_config_dir(configDir, sizeof(configDir));

    std::string dir = std::string(configDir) + "\\" + BACKUP_DIR;
    CreateDirectoryA(dir.c_str(), NULL);
    return dir;
}

/*
 * Xóa bớt các file backup cũ, chỉ giữ lại BACKUP_KEEP_COUNT file gần
 * nhất. Vì tên file có định dạng backup_YYYYMMDD_HHMMSS.json nên sắp
 * xếp theo thứ tự chữ cái cũng chính là sắp xếp theo thời gian.
 */
void cleanupOldBackups(const std::string& backupDir)
{
    const std::string pattern = backupDir + "\\backup_*.json";

    std::vector<std::string> names;

    WIN32_FIND_DATAA fd;
    FindHandle h(FindFirstFileA(pattern.c_str(), &fd));

    if (!h.valid())
        return;

    do
    {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            names.emplace_back(fd.cFileName);
    } while (FindNextFileA(h.get(), &fd));

    std::sort(names.begin(), names.end());

    const int toDelete = static_cast<int>(names.size()) - BACKUP_KEEP_COUNT;

    for (int i = 0; i < toDelete; i++)
    {
        const std::string fullPath = backupDir + "\\" + names[static_cast<size_t>(i)];
        DeleteFileA(fullPath.c_str());
    }
}

} // namespace

int backup_create_snapshot(void)
{
    const std::string backupDir = getBackupDir();

    time_t now = time(NULL);
    struct tm t;
    localtime_s(&t, &now);

    char filepathBuf[MAX_PATH];
    snprintf(
        filepathBuf,
        sizeof(filepathBuf),
        "%s\\backup_%04d%02d%02d_%02d%02d%02d.json",
        backupDir.c_str(),
        t.tm_year + 1900,
        t.tm_mon + 1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec
    );

    const int ok = db_export_json(filepathBuf);

    if (ok)
    {
        wprintf(L"[BACKUP] Da sao luu du lieu vao %hs\n", filepathBuf);
        cleanupOldBackups(backupDir);
    }
    else
    {
        wprintf(L"[BACKUP][%hs] Sao luu that bai\n", ERR_DB_EXPORT_FAIL);
    }

    return ok;
}
