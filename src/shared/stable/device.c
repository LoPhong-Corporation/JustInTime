//
// device.c
//
// Chế độ STABLE (C core). Xem src/shared/experimental/device.cpp cho
// bản C++ tương đương (cùng interface device.h).
//
// device_id là 1 chuỗi ngẫu nhiên dạng "PC-XXXXXXXX" (8 ký tự hex),
// không liên quan gì tới tên máy Windows thật - xem device.h để biết
// đầy đủ lý do (tránh rò rỉ thông tin cá nhân).
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
