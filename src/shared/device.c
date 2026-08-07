//
// device.c
//
// TRƯỚC ĐÂY: device_id = GetComputerNameA() thẳng, tức là tên máy
// Windows thật của người dùng (ví dụ "PHONG-LAPTOP", hay tệ hơn là
// tên đầy đủ nếu ai đó đặt tên máy kiểu "NGUYEN-VAN-A-PC"). Giá trị
// này được ghi vào MỌI activity_logs, đồng bộ lên Supabase, và hiển
// thị thẳng trên dashboard (kể cả cho tài khoản phụ huynh xem máy
// con) - rò rỉ thông tin cá nhân không cần thiết.
//
// BÂY GIỜ: device_id là 1 chuỗi ngẫu nhiên dạng "PC-XXXXXXXX" (8 ký
// tự hex), không liên quan gì tới tên máy Windows thật. Sinh 1 LẦN
// DUY NHẤT ở lần chạy đầu tiên, lưu vào
//   %APPDATA%\JustInTime\device.id
// rồi dùng lại y nguyên ở các lần sau (để đồng bộ/nhắn tin liên máy
// vẫn hoạt động đúng - dashboard-go đọc lại đúng file này). Người
// dùng có thể tự đặt thêm 1 "nhãn hiển thị" (device_get_label /
// device_set_label) để dễ nhận ra máy nào là máy nào trên dashboard,
// nhưng đây là lựa chọn CHỦ ĐỘNG của người dùng - không bao giờ tự
// động lấy tên máy Windows làm giá trị mặc định.
//

#include "device.h"
#include "settings.h"

#include <windows.h>
#include <wincrypt.h>

#include <stdio.h>
#include <string.h>

#define DEVICE_ID_LEN 32

static CRITICAL_SECTION g_device_lock;
static int              g_device_lock_ready = 0;

static char g_cached_id[DEVICE_ID_LEN]      = {0};
static char g_cached_label[128]             = {0};
static int  g_cache_loaded                  = 0;

static void ensure_device_lock(void)
{
    if (!g_device_lock_ready)
    {
        InitializeCriticalSection(&g_device_lock);
        g_device_lock_ready = 1;
    }
}

static int get_device_file_path(char* out, int out_size)
{
    char dir[MAX_PATH];

    if (!settings_get_config_dir(dir, sizeof(dir)))
        return 0;

    snprintf(out, out_size, "%s\\device.id", dir);
    return 1;
}

/*
 * Sinh 4 byte ngẫu nhiên bằng CSPRNG của Windows (không dùng rand()
 * - không đủ ngẫu nhiên cho việc này) rồi in ra dạng hex 8 ký tự.
 */
static void generate_random_id(char* out, int out_size)
{
    BYTE rnd[4] = {0};
    HCRYPTPROV prov = 0;

    if (CryptAcquireContextW(
            &prov, NULL, NULL,
            PROV_RSA_FULL,
            CRYPT_VERIFYCONTEXT
        ) &&
        CryptGenRandom(prov, sizeof(rnd), rnd))
    {
        CryptReleaseContext(prov, 0);
    }
    else
    {
        /* Cực hiếm khi xảy ra, nhưng vẫn phải trả về 1 giá trị nào
         * đó thay vì để buffer rỗng - fallback dùng thời gian +
         * GetCurrentProcessId() làm nguồn ngẫu nhiên yếu hơn. */
        DWORD t = GetTickCount();
        DWORD pid = GetCurrentProcessId();
        rnd[0] = (BYTE)(t & 0xFF);
        rnd[1] = (BYTE)((t >> 8) & 0xFF);
        rnd[2] = (BYTE)(pid & 0xFF);
        rnd[3] = (BYTE)((pid >> 8) & 0xFF);
    }

    snprintf(
        out, out_size,
        "PC-%02X%02X%02X%02X",
        rnd[0], rnd[1], rnd[2], rnd[3]
    );
}

/*
 * Đọc file device.id (2 dòng: id, rồi label - label có thể vắng
 * mặt/rỗng). Nếu file không tồn tại hoặc dòng đầu không đúng định
 * dạng "PC-XXXXXXXX", coi như chưa có gì, sinh mới và ghi lại.
 */
static void load_or_create(void)
{
    if (g_cache_loaded)
        return;

    char path[MAX_PATH] = {0};
    g_cached_id[0]    = '\0';
    g_cached_label[0] = '\0';

    if (get_device_file_path(path, sizeof(path)))
    {
        FILE* f = NULL;
        fopen_s(&f, path, "r");

        if (f)
        {
            char line1[DEVICE_ID_LEN] = {0};
            char line2[128] = {0};

            if (fgets(line1, sizeof(line1), f))
            {
                line1[strcspn(line1, "\r\n")] = '\0';

                if (strncmp(line1, "PC-", 3) == 0 && strlen(line1) == 11)
                    strncpy_s(g_cached_id, sizeof(g_cached_id), line1, _TRUNCATE);
            }

            if (fgets(line2, sizeof(line2), f))
            {
                line2[strcspn(line2, "\r\n")] = '\0';
                strncpy_s(g_cached_label, sizeof(g_cached_label), line2, _TRUNCATE);
            }

            fclose(f);
        }
    }

    if (g_cached_id[0] == '\0')
    {
        generate_random_id(g_cached_id, sizeof(g_cached_id));

        FILE* f = NULL;

        if (path[0] != '\0')
            fopen_s(&f, path, "w");

        if (f)
        {
            fprintf(f, "%s\n%s\n", g_cached_id, g_cached_label);
            fclose(f);
        }
    }

    g_cache_loaded = 1;
}

void get_device_id(
    char* buffer,
    int size)
{
    if (!buffer || size <= 0)
        return;

    ensure_device_lock();
    EnterCriticalSection(&g_device_lock);

    load_or_create();
    strncpy_s(buffer, size, g_cached_id, _TRUNCATE);

    LeaveCriticalSection(&g_device_lock);
}

void device_get_label(
    char* buffer,
    int size)
{
    if (!buffer || size <= 0)
        return;

    ensure_device_lock();
    EnterCriticalSection(&g_device_lock);

    load_or_create();

    if (g_cached_label[0] != '\0')
    {
        strncpy_s(buffer, size, g_cached_label, _TRUNCATE);
    }
    else
    {
        /*
         * KHÔNG dùng tên máy Windows làm mặc định - chỉ dùng 1
         * nhãn chung chung kèm 4 ký tự cuối của device_id, đủ để
         * phân biệt nhiều máy trên cùng 1 tài khoản mà không tiết
         * lộ gì về người dùng.
         */
        const char* suffix = g_cached_id;
        int len = (int)strlen(g_cached_id);

        if (len >= 4)
            suffix = g_cached_id + (len - 4);

        snprintf(buffer, size, "Computer %s", suffix);
    }

    LeaveCriticalSection(&g_device_lock);
}

int device_set_label(
    const char* label)
{
    ensure_device_lock();
    EnterCriticalSection(&g_device_lock);

    load_or_create();

    strncpy_s(g_cached_label, sizeof(g_cached_label), label ? label : "", _TRUNCATE);

    char path[MAX_PATH] = {0};
    int ok = 0;

    if (get_device_file_path(path, sizeof(path)))
    {
        FILE* f = NULL;
        fopen_s(&f, path, "w");

        if (f)
        {
            fprintf(f, "%s\n%s\n", g_cached_id, g_cached_label);
            fclose(f);
            ok = 1;
        }
    }

    LeaveCriticalSection(&g_device_lock);
    return ok;
}
