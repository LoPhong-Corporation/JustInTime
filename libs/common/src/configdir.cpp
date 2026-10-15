#include "configdir.h"

#include <windows.h>
#include <cstdio>

namespace jit {

int configDirW(wchar_t* out, int outChars)
{
    wchar_t appdata[MAX_PATH] = {0};
    const DWORD n = GetEnvironmentVariableW(L"APPDATA", appdata, MAX_PATH);

    if (n == 0 || n >= MAX_PATH)
        return 0;

    if (swprintf(out, static_cast<size_t>(outChars), L"%ls\\JustInTime", appdata) < 0)
        return 0;

    CreateDirectoryW(out, NULL);
    return 1;
}

} // namespace jit
