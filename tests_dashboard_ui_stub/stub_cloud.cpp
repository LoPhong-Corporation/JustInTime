#include "cloud.h"
#include <thread>
#include <chrono>
namespace jit::dash::cloud {
static void fakeDelay() { std::this_thread::sleep_for(std::chrono::milliseconds(120)); }

Session login(const std::string& email, const std::string&)
{
    fakeDelay();
    if (email == "fail@test.com") throw AuthError("Invalid login credentials");
    return {"tok-abc", "rt-abc", "user-1", email};
}
void logout(const std::string&, const std::string&) { fakeDelay(); }
Session refreshSession(const std::string&) { fakeDelay(); return {"tok-new", "rt-new", "user-1", "demo@test.com"}; }
Session sessionFromOAuthTokens(const std::string&, const std::string&) { throw AuthError("not used in stub"); }

void Client::changePassword(const std::string&) { fakeDelay(); }

std::vector<Client::DailyTotal> Client::dailyTotals(const std::string&, const std::string&)
{
    fakeDelay();
    return {{"PC-TESTFAKE1", "chrome.exe", "2026-09-27", 5400}, {"PC-TESTFAKE1", "code.exe", "2026-09-27", 9000},
            {"PC-OTHERDEV2", "chrome.exe", "2026-09-26", 3600}, {"PC-TESTFAKE1", "chrome.exe", "2026-09-26", 4200}};
}
std::vector<Client::RecentLog> Client::recentLogs(int)
{
    fakeDelay();
    return {{"PC-TESTFAKE1", "chrome.exe", "Gmail - Inbox", 1800, 1758960000, 1758961800},
            {"PC-OTHERDEV2", "code.exe", "main.cpp", 3600, 1758950000, 1758953600}};
}
std::vector<Client::RecentLog> Client::activityLogsForChild(const std::string&, int64_t, int)
{
    fakeDelay();
    return {{"PC-CHILD1", "game.exe", "Some Game", 7200, 1758960000, 1758967200}};
}
void Client::ping() { fakeDelay(); }
void Client::sendMessage(const Message&) { fakeDelay(); }
std::vector<Client::Message> Client::inbox(const std::string&, int) { fakeDelay(); return {}; }
std::vector<Client::Message> Client::thread(const std::string&, const std::string&, int)
{
    fakeDelay();
    return {{1, "PC-OTHERDEV2", "PC-TESTFAKE1", "message", "Hey, are you using the shared laptop?", "2026-09-27T10:00:00Z", std::nullopt}};
}
void Client::markRead(int64_t) { fakeDelay(); }
void Client::inviteChild(const std::string&) { fakeDelay(); }
std::vector<Client::ParentLink> Client::listLinksAsParent()
{
    fakeDelay();
    return {{1, "user-child-1", "child@example.com", "approved", "2026-08-01T00:00:00Z", "2026-08-01T00:00:00Z"},
            {2, "user-child-2", "pending@example.com", "pending", "2026-09-20T00:00:00Z", std::nullopt}};
}
std::vector<Client::ParentLink> Client::listLinksAsChild()
{
    fakeDelay();
    return {{3, "user-parent-1", "parent@example.com", "approved", "2026-07-01T00:00:00Z", "2026-07-01T00:00:00Z"}};
}
void Client::approveLink(int64_t) { fakeDelay(); }
void Client::revokeLink(int64_t) { fakeDelay(); }
std::vector<std::pair<int64_t, std::string>> Client::listPermissions() { fakeDelay(); return {{1, "full"}}; }
void Client::setPermission(int64_t, const std::string&) { fakeDelay(); }
std::vector<Client::AppLimit> Client::listLimitsForChild(const std::string&)
{
    fakeDelay();
    return {{1, "game.exe", 7200, false}, {2, "tiktok.exe", std::nullopt, true}};
}
void Client::setLimit(const std::string&, const std::string&, std::optional<int>, bool) { fakeDelay(); }
void Client::deleteLimit(int64_t) { fakeDelay(); }
void Client::pushHeartbeat(const std::string&, const std::string&, double, double, double) { fakeDelay(); }
std::vector<Client::DeviceHeartbeat> Client::listHeartbeats()
{
    fakeDelay();
    // "now" và "cũ" tính động (không hardcode ngày) để demo đúng cả 3
    // trạng thái (online/critical/offline) bất kể lúc nào chạy test này.
    char nowBuf[32], oldBuf[32];
    time_t now = time(nullptr);
    struct tm tmNow; gmtime_r(&now, &tmNow);
    strftime(nowBuf, sizeof(nowBuf), "%Y-%m-%dT%H:%M:%SZ", &tmNow);
    time_t old = now - 3600 * 24 * 3;
    struct tm tmOld; gmtime_r(&old, &tmOld);
    strftime(oldBuf, sizeof(oldBuf), "%Y-%m-%dT%H:%M:%SZ", &tmOld);

    return {{"PC-TESTFAKE1", "Test-PC-Overview", nowBuf, 42.0, 58.0, 97.1},
            {"PC-OTHERDEV2", "Laptop-Phong", nowBuf, 88.0, 91.0, 60.0},
            {"PC-OLDDEV3", "Old-Desktop", oldBuf, 10.0, 20.0, 30.0}};
}
std::vector<Client::DeviceHeartbeat> Client::listHeartbeatsForChild(const std::string&)
{
    fakeDelay();
    return {{"PC-CHILD1", "Kid-Laptop", "2026-09-27T12:00:00Z", 30.0, 45.0, 55.0}};
}
void Client::deleteHeartbeat(const std::string&) { fakeDelay(); }
}
