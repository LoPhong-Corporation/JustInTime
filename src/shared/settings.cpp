//
// settings.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface
// extern "C" trong settings.h), hành vi 100% giống bản C gốc trước khi chuyển đổi. Đổi
// cách viết:
//   - std::mutex (magic static) thay CRITICAL_SECTION tự quản -
//     cùng lý do đã nêu ở device.cpp (tránh race condition lý
//     thuyết lúc khởi tạo lần đầu).
//   - std::map<std::string, std::string> để parse file .ini thay vì
//     chuỗi if/else so sánh từng key thủ công - thêm 1 setting mới
//     chỉ cần thêm 1 dòng trong bảng ánh xạ, không cần sửa parser.
//   - std::ifstream/std::ofstream thay FILE* - tự đóng file (RAII),
//     không cần fclose() thủ công ở mọi nhánh return.
//

#include "settings.h"
#include "config.h"

#include <windows.h>
#include <wincrypt.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <map>
#include <mutex>
#include <fstream>
#include <sstream>

namespace {

std::mutex& settingsMutex()
{
    static std::mutex m;
    return m;
}

AppSettings g_settings;

void setDefaults(AppSettings* s)
{
    memset(s, 0, sizeof(AppSettings));

    s->sync_interval_sec    = 30;
    s->backup_interval_sec  = 3600;
    s->summary_interval_sec = 300;
    s->min_duration_sec     = 2;
    s->autostart_enabled    = 0;
    s->app_role             = APP_ROLE_CHILD;
    s->language             = APP_LANG_EN;

    s->retry_backoff_base_sec = 10;
    s->retry_backoff_max_sec  = 1800;

    s->remote_view_enabled = 0;
    s->remote_view_port    = 8787;
    s->remote_view_token[0] = '\0';

    s->excluded_processes[0] = '\0';
    s->supabase_url[0]       = '\0';
    s->supabase_key[0]       = '\0';
}

bool getSettingsPath(std::string& out)
{
    char dir[MAX_PATH] = {0};

    if (!settings_get_config_dir(dir, sizeof(dir)))
        return false;

    out = std::string(dir) + "\\settings.ini";
    return true;
}

std::string trimNewline(std::string s)
{
    while (!s.empty() && (s.back() == '\r' || s.back() == '\n'))
        s.pop_back();
    return s;
}

std::string trimLeadingSpace(std::string s)
{
    size_t i = 0;
    while (i < s.size() && s[i] == ' ')
        i++;
    return s.substr(i);
}

/*
 * Đọc toàn bộ file .ini vào 1 bảng key->value - đơn giản hơn nhiều
 * so với if/else nối tiếp của bản C, và dễ mở rộng thêm setting mới.
 */
std::map<std::string, std::string> parseIniFile(const std::string& path)
{
    std::map<std::string, std::string> out;

    std::ifstream f(path);
    if (!f)
        return out;

    std::string line;
    while (std::getline(f, line))
    {
        line = trimNewline(line);

        if (line.empty() || line[0] == ';' || line[0] == '#')
            continue;

        size_t eq = line.find('=');
        if (eq == std::string::npos)
            continue;

        out[line.substr(0, eq)] = line.substr(eq + 1);
    }

    return out;
}

void applyIniValues(AppSettings* s, const std::map<std::string, std::string>& ini)
{
    auto getInt = [&](const char* key, int fallback) -> int
    {
        auto it = ini.find(key);
        return it != ini.end() ? std::atoi(it->second.c_str()) : fallback;
    };
    auto getStr = [&](const char* key, char* buf, size_t bufSize)
    {
        auto it = ini.find(key);
        if (it != ini.end())
            snprintf(buf, bufSize, "%s", it->second.c_str());
    };

    s->sync_interval_sec    = getInt("sync_interval_sec", s->sync_interval_sec);
    s->backup_interval_sec  = getInt("backup_interval_sec", s->backup_interval_sec);
    s->summary_interval_sec = getInt("summary_interval_sec", s->summary_interval_sec);
    s->min_duration_sec     = getInt("min_duration_sec", s->min_duration_sec);
    s->autostart_enabled    = getInt("autostart_enabled", s->autostart_enabled);
    s->app_role             = getInt("app_role", s->app_role);
    s->language             = getInt("language", s->language);
    s->retry_backoff_base_sec = getInt("retry_backoff_base_sec", s->retry_backoff_base_sec);
    s->retry_backoff_max_sec  = getInt("retry_backoff_max_sec", s->retry_backoff_max_sec);
    s->remote_view_enabled  = getInt("remote_view_enabled", s->remote_view_enabled);
    s->remote_view_port     = getInt("remote_view_port", s->remote_view_port);

    getStr("remote_view_token", s->remote_view_token, sizeof(s->remote_view_token));
    getStr("excluded_processes", s->excluded_processes, MAX_EXCLUDED_LEN);
    getStr("supabase_url", s->supabase_url, MAX_URL_LEN);
    getStr("supabase_key", s->supabase_key, MAX_KEY_LEN);
}

std::string generateRemoteViewToken()
{
    unsigned char randomBytes[16];
    HCRYPTPROV hProv = 0;

    if (
        CryptAcquireContextW(&hProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)
    )
    {
        CryptGenRandom(hProv, sizeof(randomBytes), randomBytes);
        CryptReleaseContext(hProv, 0);

        std::string token;
        char byteBuf[3];
        for (unsigned char b : randomBytes)
        {
            snprintf(byteBuf, sizeof(byteBuf), "%02x", b);
            token += byteBuf;
        }
        return token;
    }

    /* Fallback hiếm khi xảy ra nếu CryptAcquireContext lỗi */
    char buf[32];
    snprintf(buf, sizeof(buf), "%08x%08x",
        static_cast<unsigned int>(GetTickCount()),
        static_cast<unsigned int>(GetCurrentProcessId()));
    return buf;
}

} // namespace

int settings_get_config_dir(
    char* out,
    int out_size)
{
    const char* appdata = getenv("APPDATA");

    if (!appdata)
        return 0;

    snprintf(out, out_size, "%s\\JustInTime", appdata);
    CreateDirectoryA(out, NULL);

    return 1;
}

int settings_save(const AppSettings* s)
{
    std::string path;
    if (!getSettingsPath(path))
        return 0;

    std::ofstream f(path, std::ios::out | std::ios::trunc);
    if (!f)
        return 0;

    f << "; JustInTime Agent - file cau hinh\n";
    f << "sync_interval_sec=" << s->sync_interval_sec << "\n";
    f << "backup_interval_sec=" << s->backup_interval_sec << "\n";
    f << "summary_interval_sec=" << s->summary_interval_sec << "\n";
    f << "min_duration_sec=" << s->min_duration_sec << "\n";
    f << "autostart_enabled=" << s->autostart_enabled << "\n";
    f << "app_role=" << s->app_role << "\n";
    f << "language=" << s->language << "\n";
    f << "excluded_processes=" << s->excluded_processes << "\n";
    f << "supabase_url=" << s->supabase_url << "\n";
    f << "supabase_key=" << s->supabase_key << "\n";
    f << "retry_backoff_base_sec=" << s->retry_backoff_base_sec << "\n";
    f << "retry_backoff_max_sec=" << s->retry_backoff_max_sec << "\n";
    f << "remote_view_enabled=" << s->remote_view_enabled << "\n";
    f << "remote_view_port=" << s->remote_view_port << "\n";
    f << "remote_view_token=" << s->remote_view_token << "\n";

    return f.good() ? 1 : 0;
}

void settings_load(AppSettings* out)
{
    setDefaults(out);

    std::string path;

    if (!getSettingsPath(path))
    {
        std::lock_guard<std::mutex> lock(settingsMutex());
        g_settings = *out;
        return;
    }

    std::ifstream probe(path);
    if (probe)
    {
        probe.close();
        const auto ini = parseIniFile(path);
        applyIniValues(out, ini);
    }
    else
    {
        /* Chưa có file, tạo mới với giá trị mặc định. */
        settings_save(out);
    }

    /*
     * Tự sinh token ngẫu nhiên cho Remote View nếu chưa có (lần đầu
     * chạy, hoặc token bị xóa trắng).
     */
    if (out->remote_view_token[0] == '\0')
    {
        const std::string token = generateRemoteViewToken();
        snprintf(out->remote_view_token, sizeof(out->remote_view_token), "%s", token.c_str());
        settings_save(out);
    }

    std::lock_guard<std::mutex> lock(settingsMutex());
    g_settings = *out;
}

void settings_get(AppSettings* out)
{
    std::lock_guard<std::mutex> lock(settingsMutex());
    *out = g_settings;
}

int settings_update(const AppSettings* s)
{
    {
        std::lock_guard<std::mutex> lock(settingsMutex());
        g_settings = *s;
    }

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
        char exePath[MAX_PATH];
        DWORD len = GetModuleFileNameA(NULL, exePath, MAX_PATH);

        if (len == 0 || len == MAX_PATH)
        {
            RegCloseKey(hKey);
            return 0;
        }

        RegSetValueExA(
            hKey, "JustInTimeAgent", 0, REG_SZ,
            reinterpret_cast<const BYTE*>(exePath),
            static_cast<DWORD>(strlen(exePath)) + 1
        );
    }
    else
    {
        RegDeleteValueA(hKey, "JustInTimeAgent");
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

    char processUtf8[512] = {0};
    WideCharToMultiByte(
        CP_UTF8, 0, process_name, -1,
        processUtf8, sizeof(processUtf8),
        NULL, NULL
    );

    std::istringstream iss(s.excluded_processes);
    std::string token;

    while (std::getline(iss, token, ','))
    {
        token = trimLeadingSpace(token);

        if (_stricmp(token.c_str(), processUtf8) == 0)
            return 1;
    }

    return 0;
}

void settings_get_supabase_config(
    char* url_out, int url_out_size,
    char* key_out, int key_out_size)
{
    AppSettings s;
    settings_get(&s);

    snprintf(url_out, url_out_size, "%s", s.supabase_url[0] != '\0' ? s.supabase_url : SUPABASE_URL);
    snprintf(key_out, key_out_size, "%s", s.supabase_key[0] != '\0' ? s.supabase_key : SUPABASE_ANON_KEY);
}
