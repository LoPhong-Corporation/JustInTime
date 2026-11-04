//
// httpclient.cpp - xem httpclient.h
//

#include "httpclient.h"
#include "log.h"

#include <winhttp.h>

#pragma comment(lib, "winhttp.lib")

namespace jit {

namespace {

class Handle
{
public:
    Handle() = default;
    explicit Handle(HINTERNET h) : m_h(h) {}
    ~Handle() { close(); }

    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;

    Handle(Handle&& o) noexcept : m_h(o.m_h) { o.m_h = nullptr; }
    Handle& operator=(Handle&& o) noexcept
    {
        if (this != &o)
        {
            close();
            m_h = o.m_h;
            o.m_h = nullptr;
        }
        return *this;
    }

    HINTERNET get() const { return m_h; }
    explicit operator bool() const { return m_h != nullptr; }

private:
    void close()
    {
        if (m_h)
            WinHttpCloseHandle(m_h);
        m_h = nullptr;
    }

    HINTERNET m_h = nullptr;
};

// Timeout (ms): resolve, connect, send, receive.
constexpr int kResolveMs = 5000;
constexpr int kConnectMs = 8000;
constexpr int kSendMs    = 15000;
constexpr int kReceiveMs = 20000;

struct Connection
{
    Handle       session;
    Handle       connect;
    std::wstring host;

    void reset()
    {
        // Đóng connect trước, session sau (thứ tự ngược với lúc tạo).
        connect = Handle();
        session = Handle();
        host.clear();
    }

    bool ensure(const std::wstring& wantedHost)
    {
        if (connect && host == wantedHost)
            return true;

        reset();

        session = Handle(WinHttpOpen(
            L"JustInTime-Agent/1.0",
            WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS,
            0));

        if (!session)
        {
            JIT_LOG(L"[HTTP] WinHttpOpen that bai (%lu)\n", GetLastError());
            return false;
        }

        WinHttpSetTimeouts(session.get(), kResolveMs, kConnectMs, kSendMs, kReceiveMs);

        connect = Handle(WinHttpConnect(
            session.get(), wantedHost.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));

        if (!connect)
        {
            JIT_LOG(L"[HTTP] WinHttpConnect that bai (%lu)\n", GetLastError());
            session = Handle();
            return false;
        }

        host = wantedHost;
        return true;
    }
};

// Mỗi thread có 1 kết nối riêng => không cần mutex, và luồng GUI không bao
// giờ phải xếp hàng sau luồng sync. Tự đóng khi thread kết thúc.
thread_local Connection t_conn;

bool requestOnce(
    const std::wstring& host,
    const wchar_t* method,
    const std::wstring& path,
    const std::wstring& headers,
    const std::string& body,
    HttpResult& out,
    size_t maxBody)
{
    if (!t_conn.ensure(host))
        return false;

    Handle request(WinHttpOpenRequest(
        t_conn.connect.get(), method, path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));

    if (!request)
    {
        JIT_LOG(L"[HTTP] WinHttpOpenRequest that bai (%lu)\n", GetLastError());
        t_conn.reset();
        return false;
    }

    const DWORD bodyLen = static_cast<DWORD>(body.size());

    const BOOL sent = WinHttpSendRequest(
        request.get(),
        headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(),
        static_cast<DWORD>(headers.size()),
        bodyLen ? const_cast<char*>(body.data()) : WINHTTP_NO_REQUEST_DATA,
        bodyLen, bodyLen, 0);

    if (!sent || !WinHttpReceiveResponse(request.get(), nullptr))
    {
        JIT_LOG(L"[HTTP] Gui/nhan request that bai (%lu)\n", GetLastError());
        t_conn.reset(); // kết nối có thể đã hỏng - lần sau dựng lại
        return false;
    }

    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    WinHttpQueryHeaders(
        request.get(),
        WINHTTP_QUERY_FLAG_NUMBER | WINHTTP_QUERY_STATUS_CODE,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);

    out.status = status;
    out.body.clear();

    for (;;)
    {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available) || available == 0)
            break;

        if (out.body.size() >= maxBody)
            break; // đủ rồi; bỏ phần còn lại (kết nối sẽ được WinHTTP dọn)

        if (available > maxBody - out.body.size())
            available = static_cast<DWORD>(maxBody - out.body.size());

        const size_t offset = out.body.size();
        out.body.resize(offset + available);

        DWORD read = 0;
        if (!WinHttpReadData(request.get(), out.body.data() + offset, available, &read) || read == 0)
        {
            out.body.resize(offset);
            break;
        }

        out.body.resize(offset + read);
    }

    return status != 0;
}

} // namespace

bool httpsRequest(
    const std::wstring& host,
    const wchar_t* method,
    const std::wstring& path,
    const std::wstring& extraHeaders,
    const std::string& body,
    HttpResult& out,
    bool idempotent,
    size_t maxBody)
{
    const int attempts = idempotent ? 2 : 1;

    for (int i = 0; i < attempts; i++)
    {
        if (requestOnce(host, method, path, extraHeaders, body, out, maxBody))
            return true;
    }

    return false;
}

} // namespace jit
