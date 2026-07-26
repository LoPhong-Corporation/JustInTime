//
// settings.c
//

#include "settings.h"
#include "config.h"

#include <windows.h>
#include <wincrypt.h>

#include <stdio.h>
#include <string.h>
#include <wchar.h>

static AppSettings   g_settings;
static CRITICAL_SECTION g_settings_lock;
static int           g_settings_lock_ready = 0;

static void ensure_lock(void)
{
    if (!g_settings_lock_ready)
    {
        InitializeCriticalSection(&g_settings_lock);
        g_settings_lock_ready = 1;
    }
}

static void set_defaults(AppSettings* s)
{
    memset(s, 0, sizeof(AppSettings));

    s->sync_interval_sec    = 30;
    s->backup_interval_sec  = 3600;
    s->summary_interval_sec = 300;
    s->min_duration_sec     = 2;
    s->autostart_enabled    = 0;
    s->app_role             = APP_ROLE_CHILD;

    s->retry_backoff_base_sec = 10;   /* lần đầu thất bại: chờ 10s */
    s->retry_backoff_max_sec  = 1800; /* tối đa chờ 30 phút giữa 2 lần thử */

    s->remote_view_enabled = 0;
    s->remote_view_port    = 8787;
    s->remote_view_token[0] = '\0';

    s->excluded_processes[0] = '\0';
    s->supabase_url[0]       = '\0';
    s->supabase_key[0]       = '\0';
}

int settings_get_config_dir(
    char* out,
    int out_size)
{
    const char* appdata = getenv("APPDATA");

    if (!appdata)
        return 0;

    snprintf(
        out,
        out_size,
        "%s\\JustInTime",
        appdata
    );

    CreateDirectoryA(out, NULL);

    return 1;
}

static int get_settings_path(
    char* out,
    int out_size)
{
    char dir[MAX_PATH];

    if (!settings_get_config_dir(dir, sizeof(dir)))
        return 0;

    snprintf(
        out,
        out_size,
        "%s\\settings.ini",
        dir
    );

    return 1;
}

/*
 * Parser INI rất đơn giản: mỗi dòng "key=value",
 * dòng bắt đầu bằng ';' hoặc '#' là comment.
 */
static void parse_line(
    AppSettings* s,
    char* line)
{
    char* eq = strchr(line, '=');

    if (!eq)
        return;

    *eq = '\0';

    char* key = line;
    char* value = eq + 1;

    /* Bỏ ký tự xuống dòng cuối value */
    size_t len = strlen(value);

    while (
        len > 0 &&
        (value[len - 1] == '\n' || value[len - 1] == '\r')
    )
    {
        value[len - 1] = '\0';
        len--;
    }

    if (strcmp(key, "sync_interval_sec") == 0)
        s->sync_interval_sec = atoi(value);
    else if (strcmp(key, "backup_interval_sec") == 0)
        s->backup_interval_sec = atoi(value);
    else if (strcmp(key, "summary_interval_sec") == 0)
        s->summary_interval_sec = atoi(value);
    else if (strcmp(key, "min_duration_sec") == 0)
        s->min_duration_sec = atoi(value);
    else if (strcmp(key, "autostart_enabled") == 0)
        s->autostart_enabled = atoi(value);
    else if (strcmp(key, "app_role") == 0)
        s->app_role = atoi(value);
    else if (strcmp(key, "retry_backoff_base_sec") == 0)
        s->retry_backoff_base_sec = atoi(value);
    else if (strcmp(key, "retry_backoff_max_sec") == 0)
        s->retry_backoff_max_sec = atoi(value);
    else if (strcmp(key, "remote_view_enabled") == 0)
        s->remote_view_enabled = atoi(value);
    else if (strcmp(key, "remote_view_port") == 0)
        s->remote_view_port = atoi(value);
    else if (strcmp(key, "remote_view_token") == 0)
        snprintf(s->remote_view_token, sizeof(s->remote_view_token), "%s", value);
    else if (strcmp(key, "excluded_processes") == 0)
        snprintf(s->excluded_processes, MAX_EXCLUDED_LEN, "%s", value);
    else if (strcmp(key, "supabase_url") == 0)
        snprintf(s->supabase_url, MAX_URL_LEN, "%s", value);
    else if (strcmp(key, "supabase_key") == 0)
        snprintf(s->supabase_key, MAX_KEY_LEN, "%s", value);
}

void settings_load(AppSettings* out)
{
    ensure_lock();

    set_defaults(out);

    char path[MAX_PATH];

    if (!get_settings_path(path, sizeof(path)))
    {
        EnterCriticalSection(&g_settings_lock);
        g_settings = *out;
        LeaveCriticalSection(&g_settings_lock);
        return;
    }

    FILE* f = fopen(path, "r");

    if (f)
    {
        char line[2048];

        while (fgets(line, sizeof(line), f))
        {
            if (line[0] == ';' || line[0] == '#' || line[0] == '\n')
                continue;

            parse_line(out, line);
        }

        fclose(f);
    }
    else
    {
        /*
         * Chưa có file, tạo mới với giá trị mặc định.
         */
        settings_save(out);
    }

    /*
     * Tự sinh token ngẫu nhiên cho Remote View nếu chưa có
     * (lần đầu chạy, hoặc token bị xóa trắng).
     */
    if (out->remote_view_token[0] == '\0')
    {
        HCRYPTPROV hProv = 0;
        unsigned char randomBytes[16];

        if (
            CryptAcquireContextW(
                &hProv, NULL, NULL,
                PROV_RSA_FULL,
                CRYPT_VERIFYCONTEXT
            )
        )
        {
            CryptGenRandom(hProv, sizeof(randomBytes), randomBytes);
            CryptReleaseContext(hProv, 0);

            for (int i = 0; i < 16; i++)
            {
                snprintf(
                    out->remote_view_token + i * 2,
                    3,
                    "%02x",
                    randomBytes[i]
                );
            }
        }
        else
        {
            /* Fallback hiếm khi xảy ra nếu CryptAcquireContext lỗi */
            snprintf(
                out->remote_view_token,
                sizeof(out->remote_view_token),
                "%08x%08x",
                (unsigned int)GetTickCount(),
                (unsigned int)GetCurrentProcessId()
            );
        }

        settings_save(out);
    }

    EnterCriticalSection(&g_settings_lock);
    g_settings = *out;
    LeaveCriticalSection(&g_settings_lock);
}

int settings_save(const AppSettings* s)
{
    char path[MAX_PATH];

    if (!get_settings_path(path, sizeof(path)))
        return 0;

    FILE* f = fopen(path, "w");

    if (!f)
        return 0;

    fprintf(f, "; JustInTime Agent - file cau hinh\n");
    fprintf(f, "sync_interval_sec=%d\n", s->sync_interval_sec);
    fprintf(f, "backup_interval_sec=%d\n", s->backup_interval_sec);
    fprintf(f, "summary_interval_sec=%d\n", s->summary_interval_sec);
    fprintf(f, "min_duration_sec=%d\n", s->min_duration_sec);
    fprintf(f, "autostart_enabled=%d\n", s->autostart_enabled);
    fprintf(f, "app_role=%d\n", s->app_role);
    fprintf(f, "excluded_processes=%s\n", s->excluded_processes);
    fprintf(f, "supabase_url=%s\n", s->supabase_url);
    fprintf(f, "supabase_key=%s\n", s->supabase_key);
    fprintf(f, "retry_backoff_base_sec=%d\n", s->retry_backoff_base_sec);
    fprintf(f, "retry_backoff_max_sec=%d\n", s->retry_backoff_max_sec);
    fprintf(f, "remote_view_enabled=%d\n", s->remote_view_enabled);
    fprintf(f, "remote_view_port=%d\n", s->remote_view_port);
    fprintf(f, "remote_view_token=%s\n", s->remote_view_token);

    fclose(f);

    return 1;
}

void settings_get(AppSettings* out)
{
    ensure_lock();

    EnterCriticalSection(&g_settings_lock);
    *out = g_settings;
    LeaveCriticalSection(&g_settings_lock);
}

int settings_update(const AppSettings* s)
{
    ensure_lock();

    EnterCriticalSection(&g_settings_lock);
    g_settings = *s;
    LeaveCriticalSection(&g_settings_lock);

    settings_apply_autostart(s->autostart_enabled);

    return settings_save(s);
}

int settings_apply_autostart(int enabled)
{
    HKEY hKey;

    LONG result = RegOpenKeyExA(
        HKEY_CURRENT_USER,
        "Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0,
        KEY_SET_VALUE,
        &hKey
    );

    if (result != ERROR_SUCCESS)
        return 0;

    if (enabled)
    {
        char exe_path[MAX_PATH];

        DWORD len = GetModuleFileNameA(
            NULL,
            exe_path,
            MAX_PATH
        );

        if (len == 0 || len == MAX_PATH)
        {
            RegCloseKey(hKey);
            return 0;
        }

        RegSetValueExA(
            hKey,
            "JustInTimeAgent",
            0,
            REG_SZ,
            (const BYTE*)exe_path,
            (DWORD)strlen(exe_path) + 1
        );
    }
    else
    {
        RegDeleteValueA(
            hKey,
            "JustInTimeAgent"
        );
    }

    RegCloseKey(hKey);

    return 1;
}

int settings_is_process_excluded(
    const wchar_t* process_name)
{
    if (!process_name)
        return 0;

    AppSettings s;
    settings_get(&s);

    if (s.excluded_processes[0] == '\0')
        return 0;

    char process_utf8[512] = {0};

    WideCharToMultiByte(
        CP_UTF8, 0,
        process_name, -1,
        process_utf8, sizeof(process_utf8),
        NULL, NULL
    );

    /*
     * So khớp không phân biệt hoa/thường, tách theo dấu phẩy.
     */
    char list_copy[MAX_EXCLUDED_LEN];
    snprintf(list_copy, sizeof(list_copy), "%s", s.excluded_processes);

    char* token = strtok(list_copy, ",");

    while (token)
    {
        /* Bỏ khoảng trắng đầu token */
        while (*token == ' ')
            token++;

        if (_stricmp(token, process_utf8) == 0)
            return 1;

        token = strtok(NULL, ",");
    }

    return 0;
}

void settings_get_supabase_config(
    char* url_out, int url_out_size,
    char* key_out, int key_out_size)
{
    AppSettings s;
    settings_get(&s);

    if (s.supabase_url[0] != '\0')
        snprintf(url_out, url_out_size, "%s", s.supabase_url);
    else
        snprintf(url_out, url_out_size, "%s", SUPABASE_URL);

    if (s.supabase_key[0] != '\0')
        snprintf(key_out, key_out_size, "%s", s.supabase_key);
    else
        snprintf(key_out, key_out_size, "%s", SUPABASE_ANON_KEY);
}
