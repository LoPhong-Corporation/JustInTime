//
// backup.c
// Backup dữ liệu cục bộ ra file JSON, tách biệt
// hoàn toàn với việc đồng bộ lên Supabase, để đảm
// bảo luôn có ít nhất một bản sao dữ liệu an toàn
// ngay cả khi mất kết nối mạng dài ngày.
//

#include "backup.h"
#include "database.h"
#include "config.h"
#include "settings.h"
#include "error_codes.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>
#include <time.h>

/*
 * Lấy đường dẫn tuyệt đối tới thư mục backup
 * (%APPDATA%\JustInTime\backups), tạo nếu chưa có.
 * QUAN TRỌNG: không dùng đường dẫn tương đối, vì khi
 * app tự khởi động cùng Windows (autostart), thư mục
 * làm việc hiện tại có thể khác thư mục chứa file .exe.
 */
static void get_backup_dir(char* out, int out_size)
{
    char config_dir[MAX_PATH];

    settings_get_config_dir(config_dir, sizeof(config_dir));

    snprintf(
        out,
        out_size,
        "%s\\%s",
        config_dir,
        BACKUP_DIR
    );

    CreateDirectoryA(out, NULL);
}

/*
 * Xóa bớt các file backup cũ, chỉ giữ lại
 * BACKUP_KEEP_COUNT file gần nhất.
 * Vì tên file có định dạng backup_YYYYMMDD_HHMMSS.json
 * nên sắp xếp theo thứ tự chữ cái cũng chính là
 * sắp xếp theo thời gian.
 */
static void cleanup_old_backups(const char* backup_dir)
{
    char pattern[MAX_PATH];

    snprintf(
        pattern,
        sizeof(pattern),
        "%s\\backup_*.json",
        backup_dir
    );

    char names[512][MAX_PATH];
    int count = 0;

    WIN32_FIND_DATAA fd;

    HANDLE h = FindFirstFileA(
        pattern,
        &fd
    );

    if (h == INVALID_HANDLE_VALUE)
        return;

    do
    {
        if (
            !(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            count < 512
        )
        {
            snprintf(
                names[count],
                MAX_PATH,
                "%s",
                fd.cFileName
            );

            count++;
        }
    } while (FindNextFileA(h, &fd));

    FindClose(h);

    /*
     * Sắp xếp tăng dần (insertion sort, count nhỏ nên ổn).
     */
    for (int i = 1; i < count; i++)
    {
        char key[MAX_PATH];

        snprintf(key, MAX_PATH, "%s", names[i]);

        int j = i - 1;

        while (j >= 0 && strcmp(names[j], key) > 0)
        {
            snprintf(names[j + 1], MAX_PATH, "%s", names[j]);
            j--;
        }

        snprintf(names[j + 1], MAX_PATH, "%s", key);
    }

    int to_delete = count - BACKUP_KEEP_COUNT;

    for (int i = 0; i < to_delete; i++)
    {
        char full_path[MAX_PATH];

        snprintf(
            full_path,
            sizeof(full_path),
            "%s\\%s",
            backup_dir,
            names[i]
        );

        DeleteFileA(full_path);
    }
}

int backup_create_snapshot(void)
{
    char backup_dir[MAX_PATH];

    get_backup_dir(backup_dir, sizeof(backup_dir));

    time_t now = time(NULL);
    struct tm t;

    localtime_s(&t, &now);

    char filepath[MAX_PATH];

    snprintf(
        filepath,
        sizeof(filepath),
        "%s\\backup_%04d%02d%02d_%02d%02d%02d.json",
        backup_dir,
        t.tm_year + 1900,
        t.tm_mon + 1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec
    );

    int ok = db_export_json(filepath);

    if (ok)
    {
        wprintf(
            L"[BACKUP] Da sao luu du lieu vao %hs\n",
            filepath
        );

        cleanup_old_backups(backup_dir);
    }
    else
    {
        wprintf(
            L"[BACKUP][%hs] Sao luu that bai\n",
            ERR_DB_EXPORT_FAIL
        );
    }

    return ok;
}
