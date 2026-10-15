#include "dashsession.h"
#include "paths.h"

#include <windows.h>
#include <wincrypt.h>

#include <fstream>
#include <sstream>
#include <vector>

#pragma comment(lib, "crypt32.lib")

namespace jit::dash {

namespace {

std::filesystem::path sessionPath() { return jit::configFile(L"dashboard_session.dat"); }

std::string dpapiProtect(const std::string& plain)
{
    DATA_BLOB in{static_cast<DWORD>(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"JustInTime dashboard session", nullptr, nullptr, nullptr, 0, &out))
        return "";
    std::string result(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return result;
}

std::string dpapiUnprotect(const std::string& enc)
{
    DATA_BLOB in{static_cast<DWORD>(enc.size()), reinterpret_cast<BYTE*>(const_cast<char*>(enc.data()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, 0, &out))
        return "";
    std::string result(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return result;
}

} // namespace

bool saveDashSession(const DashSession& s)
{
    // 4 dòng: user_id, email, access_token, refresh_token - giống hệt
    // format dashsession_windows.go để tiện gỡ lỗi thủ công nếu cần.
    const std::string plain = s.userId + "\n" + s.email + "\n" + s.accessToken + "\n" + s.refreshToken + "\n";
    const std::string enc = dpapiProtect(plain);
    if (enc.empty())
        return false;
    return jit::writeFileAtomic(sessionPath(), enc);
}

std::optional<DashSession> loadDashSession()
{
    std::ifstream f(sessionPath(), std::ios::in | std::ios::binary);
    if (!f)
        return std::nullopt;

    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string enc = ss.str();
    if (enc.empty())
        return std::nullopt;

    const std::string plain = dpapiUnprotect(enc);
    if (plain.empty())
        return std::nullopt;

    std::vector<std::string> lines;
    size_t start = 0;
    for (size_t i = 0; i <= plain.size(); i++)
    {
        if (i == plain.size() || plain[i] == '\n')
        {
            lines.push_back(plain.substr(start, i - start));
            start = i + 1;
        }
    }
    if (lines.size() < 4)
        return std::nullopt;

    DashSession s;
    s.userId = lines[0];
    s.email = lines[1];
    s.accessToken = lines[2];
    s.refreshToken = lines[3];
    return s;
}

void clearDashSession()
{
    DeleteFileW(sessionPath().c_str());
}

} // namespace jit::dash
