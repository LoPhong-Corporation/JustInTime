#include "autostart.h"

#include <windows.h>
#include <string>

namespace jit::dash {

namespace {
constexpr wchar_t kRunKeyPath[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"JustInTimeDashboard";
} // namespace

bool setAutostart(bool enabled)
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS)
        return false;

    bool ok;
    if (!enabled)
    {
        const LSTATUS rc = RegDeleteValueW(key, kValueName);
        ok = (rc == ERROR_SUCCESS || rc == ERROR_FILE_NOT_FOUND);
    }
    else
    {
        wchar_t exePath[MAX_PATH] = {0};
        GetModuleFileNameW(nullptr, exePath, MAX_PATH);

        std::wstring value = L"\"" + std::wstring(exePath) + L"\" --tray";
        ok = RegSetValueExW(key, kValueName, 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(value.c_str()),
                             static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t))) == ERROR_SUCCESS;
    }

    RegCloseKey(key);
    return ok;
}

bool isAutostartEnabled()
{
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKeyPath, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS)
        return false;

    const LSTATUS rc = RegQueryValueExW(key, kValueName, nullptr, nullptr, nullptr, nullptr);
    RegCloseKey(key);
    return rc == ERROR_SUCCESS;
}

} // namespace jit::dash
