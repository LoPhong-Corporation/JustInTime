//
// activity.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface extern "C" trong
// activity.h). Đây là module NHẠY CẢM NHẤT về đồng bộ hoá luồng (worker
// thread ghi, GUI thread + HTTP thread đọc), nên khi chuyển đổi các ranh
// giới khoá/mở khoá được giữ NGUYÊN VẸN: finishCurrentRecord() CHỦ Ý nhả
// khoá trước khi gọi settings_get()/db_insert_activity() để không giữ khoá
// trong lúc I/O.
//
// Đợt tối ưu/sửa lỗi sau đó (đều có chú thích tại chỗ):
//   1. Đọc tên process bằng PROCESS_QUERY_LIMITED_INFORMATION +
//      QueryFullProcessImageNameW. Trước đây dùng OpenProcess(VM_READ) +
//      GetModuleBaseNameW nên với app chạy "as Administrator" (hoặc bất kỳ
//      process nào không đọc được bộ nhớ) lời gọi thất bại, monitor_activity()
//      im lặng bỏ qua, và TOÀN BỘ thời gian dùng app đó bị cộng cho app
//      trước đó - đồng thời giới hạn app (kill) không nhận ra nó.
//   2. Cờ g_suspended: sau khi khoá máy/ngủ, luồng theo dõi KHÔNG được tự
//      mở record mới cho tới khi resume. Trước đây chỉ reset g_last_window,
//      nên nếu luồng theo dõi chạy thêm 1 nhịp giữa lúc nhận sự kiện và lúc
//      máy thật sự ngủ thì 1 record mới được mở ra và sau khi thức dậy nó
//      "sống tiếp", cộng cả khoảng ngủ vào thời lượng dùng app.
//   3. Sau khi chốt record, xoá luôn g_current_record: tránh activity_check_limits()
//      và remote view đọc lại app/thời điểm cũ của phiên đã đóng, và tránh
//      finishCurrentRecord() tính duration = now - 0 (hàng chục năm) nếu
//      lỡ được gọi khi chưa có record nào mở.
//   4. Toàn bộ wprintf -> JIT_LOG (không tốn công format/ghi console khi
//      không bật debug console).
//

#include "activity.h"
#include "database.h"
#include "settings.h"
#include "applimits.h"
#include "auth.h"
#include "log.h"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cwchar>
#include <ctime>
#include <cstring>
#include <mutex>

namespace {

ActiveWindow g_last_window = {};
ActivityRecord g_current_record = {};
bool g_finish_in_progress = false;

// true từ lúc nhận sự kiện khoá máy/ngủ cho tới khi mở khoá/thức dậy. Đọc/ghi
// (khi cần nhất quán với g_last_window) luôn trong lúc giữ currentMutex().
std::atomic<bool> g_suspended{false};

std::mutex& currentMutex()
{
    static std::mutex m;
    return m;
}

/*
 * Tên file (không kèm đường dẫn) của process đang mở qua `process`.
 * Handle chỉ cần PROCESS_QUERY_LIMITED_INFORMATION - quyền này được cấp
 * ngay cả cho process chạy với quyền cao hơn ta.
 */
bool getProcessBaseName(HANDLE process, wchar_t* out, DWORD outChars)
{
    wchar_t full[MAX_PATH * 2] = {0};
    DWORD size = static_cast<DWORD>(sizeof(full) / sizeof(full[0]));

    if (!QueryFullProcessImageNameW(process, 0, full, &size))
        return false;

    const wchar_t* base = wcsrchr(full, L'\\');
    base = base ? base + 1 : full;

    wcsncpy_s(out, outChars, base, _TRUNCATE);
    return out[0] != L'\0';
}

/*
 * Lấy thông tin cửa sổ hiện tại
 */
int getActiveWindowInfo(ActiveWindow* window)
{
    if (!window)
        return 0;

    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return 0;

    GetWindowTextW(
        hwnd,
        window->window_title,
        sizeof(window->window_title) / sizeof(window->window_title[0])
    );

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return 0;

    const bool success = getProcessBaseName(process, window->process_name, MAX_PATH);
    CloseHandle(process);

    return success ? 1 : 0;
}

/*
 * In activity vừa hoàn thành
 */
void printActivityReport(const ActivityRecord* record)
{
    long total = record->duration_seconds;
    long hours = total / 3600;
    long minutes = (total % 3600) / 60;
    long seconds = total % 60;

    JIT_LOG(
        L"\n"
        L"=====================================\n"
        L"[ACTIVITY]\n"
        L"Process : %ls\n"
        L"Title   : %ls\n"
        L"Used    : %02ld:%02ld:%02ld\n"
        L"=====================================\n",
        record->process_name, record->window_title, hours, minutes, seconds
    );
}

/*
 * Bắt đầu activity mới. GỌI KHI ĐÃ GIỮ currentMutex().
 */
void startNewRecordLocked(const ActiveWindow* window)
{
    wcscpy_s(g_current_record.process_name, MAX_PATH, window->process_name);
    wcscpy_s(g_current_record.window_title, 512, window->window_title);

    g_current_record.start_time = time(NULL);
    g_current_record.end_time = 0;
    g_current_record.duration_seconds = 0;
    g_current_record.synced = 0;
}

/*
 * Kết thúc activity hiện tại.
 *
 * Có "chốt khoá kép" (g_finish_in_progress) vì hàm này có thể được
 * gọi từ 2 nguồn khác nhau gần như đồng thời:
 *   - worker thread, khi phát hiện đổi cửa sổ (monitor_activity)
 *   - GUI thread, khi bắt được sự kiện khoá máy/sleep (activity_suspend,
 *     gọi từ main.cpp)
 * Nếu cả 2 xảy ra cùng lúc (vd khoá máy đúng khoảnh khắc đổi app),
 * không có chốt này thì record hiện tại có thể bị insert 2 lần vào
 * DB. Chốt bằng cờ đơn giản trong critical section: lời gọi thứ 2
 * tới trong lúc lời gọi thứ 1 chưa xong sẽ tự bỏ qua.
 */
void finishCurrentRecord()
{
    ActivityRecord recordToSave;

    {
        std::lock_guard<std::mutex> lock(currentMutex());

        if (g_finish_in_progress)
            return;

        // Không có record nào đang mở (vd đã được chốt bởi activity_suspend()
        // rồi): không có gì để lưu. Nếu không chặn ở đây, start_time = 0 sẽ cho
        // duration = now - 0 (~56 năm).
        if (g_current_record.start_time == 0)
            return;

        g_finish_in_progress = true;

        g_current_record.end_time = time(NULL);

        // Đồng hồ hệ thống có thể bị chỉnh lùi (NTP, người dùng đổi giờ): không
        // bao giờ ghi duration âm.
        g_current_record.duration_seconds =
            g_current_record.end_time > g_current_record.start_time
                ? static_cast<long>(g_current_record.end_time - g_current_record.start_time)
                : 0;

        /*
         * Chụp lại 1 bản snapshot cục bộ để dùng sau khi rời khỏi
         * critical section - tránh trường hợp startNewRecord() (từ 1
         * lời gọi khác) ghi đè g_current_record ngay trong lúc ta
         * đang exclusion-check/print/insert bên dưới.
         */
        recordToSave = g_current_record;
    }
    // <-- lock nhả ở đây (hết scope) - CHỦ Ý, để không giữ khoá trong
    //     lúc I/O (settings_is_process_excluded/settings_get/
    //     db_insert_activity bên dưới), y hệt bản C gốc trước khi chuyển đổi.

    do
    {
        /*
         * Bỏ qua app nằm trong danh sách loại trừ (cấu hình trong
         * menu Cài đặt của tray).
         */
        if (settings_is_process_excluded(recordToSave.process_name))
            break;

        /*
         * Bỏ qua record quá ngắn (ngưỡng lấy từ settings, mặc định 2
         * giây).
         */
        AppSettings s;
        settings_get(&s);

        if (recordToSave.duration_seconds < s.min_duration_sec)
            break;

        printActivityReport(&recordToSave);
        db_insert_activity(&recordToSave);
    }
    while (0);

    {
        std::lock_guard<std::mutex> lock(currentMutex());
        g_finish_in_progress = false;
    }
}

/*
 * Nếu process_name truyền vào TRÙNG với process của cửa sổ đang ở
 * foreground NGAY LÚC NÀY, kill nó. Không quét toàn bộ danh sách
 * tiến trình hệ thống - chỉ nhắm đúng cửa sổ con đang thực sự tương
 * tác, đúng với triết lý "chỉ theo dõi/tác động tới cửa sổ đang
 * active" xuyên suốt file này.
 *
 * Trả về 1 nếu đã kill, 0 nếu không tìm thấy tiến trình khớp đang ở
 * foreground (vd người dùng vừa tự chuyển app khác đúng lúc kiểm
 * tra).
 */
int killIfForeground(const wchar_t* processName)
{
    HWND hwnd = GetForegroundWindow();
    if (!hwnd)
        return 0;

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);

    HANDLE process = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
        FALSE, pid
    );
    if (!process)
        return 0;

    wchar_t currentName[MAX_PATH] = {0};
    const bool gotName = getProcessBaseName(process, currentName, MAX_PATH);

    int killed = 0;
    if (gotName && _wcsicmp(currentName, processName) == 0)
    {
        TerminateProcess(process, 1);
        killed = 1;
    }

    CloseHandle(process);
    return killed;
}

/*
 * Chốt sổ record đang mở NGAY BÂY GIỜ rồi reset trạng thái theo dõi, để
 * lần monitor_activity() kế tiếp (nếu không bị chặn bởi g_suspended) luôn
 * bắt đầu MỘT RECORD HOÀN TOÀN MỚI - dù cửa sổ active lúc đó có trùng y
 * hệt cửa sổ trước đó hay không - thay vì lặng lẽ cộng dồn khoảng thời
 * gian đã trôi qua vào record cũ.
 *
 * Dùng chung cho activity_suspend() (khoá máy/ngủ) và
 * activity_check_limits() (vừa chặn 1 app). KHÔNG đụng tới g_suspended.
 */
void closeCurrentSession()
{
    bool hasOpenRecord;
    {
        std::lock_guard<std::mutex> lock(currentMutex());
        hasOpenRecord = (g_last_window.process_name[0] != L'\0');
    }

    if (!hasOpenRecord)
        return; // chưa có record nào để chốt

    finishCurrentRecord();

    {
        std::lock_guard<std::mutex> lock(currentMutex());
        memset(&g_last_window, 0, sizeof(g_last_window));
        memset(&g_current_record, 0, sizeof(g_current_record));
    }
}

} // namespace

void activity_get_current(
    wchar_t* process_out, int process_size,
    wchar_t* title_out, int title_size,
    time_t* since_out)
{
    std::lock_guard<std::mutex> lock(currentMutex());

    if (process_out && process_size > 0)
        wcscpy_s(process_out, process_size, g_current_record.process_name);

    if (title_out && title_size > 0)
        wcscpy_s(title_out, title_size, g_current_record.window_title);

    if (since_out)
        *since_out = g_current_record.start_time;
}

void monitor_activity(void)
{
    if (g_suspended.load())
        return; // đang khoá máy/ngủ - không tự mở record mới

    ActiveWindow current = {};

    if (!getActiveWindowInfo(&current))
        return;

    bool firstRun;
    bool changed;

    {
        std::lock_guard<std::mutex> lock(currentMutex());

        if (g_suspended.load())
            return;

        firstRun = (g_last_window.process_name[0] == L'\0');
        changed = !firstRun && (
            wcscmp(current.process_name, g_last_window.process_name) != 0 ||
            wcscmp(current.window_title, g_last_window.window_title) != 0
        );

        /*
         * Lần chạy đầu tiên (bao gồm cả lần đầu tiên sau khi
         * closeCurrentSession() đã reset trạng thái vì máy vừa khoá màn
         * hình/ngủ/chặn app) - luôn bắt đầu 1 record mới. Đặt
         * g_last_window và mở record trong CÙNG 1 lần giữ khoá (trước đây
         * là 2 lần giữ khoá riêng).
         */
        if (firstRun)
        {
            g_last_window = current;
            startNewRecordLocked(&current);
        }
    }

    if (firstRun)
    {
        JIT_LOG(L"[START] %ls\n", current.process_name);
        return;
    }

    /* Nếu không thay đổi thì bỏ qua */
    if (!changed)
        return;

    /* Kết thúc record cũ */
    finishCurrentRecord();

    /* Log chuyển app */
    JIT_LOG(
        L"\n"
        L"=====================================\n"
        L"[SWITCH]\n"
        L"Process : %ls\n"
        L"Title   : %ls\n"
        L"=====================================\n",
        current.process_name, current.window_title
    );

    /* Record mới */
    {
        std::lock_guard<std::mutex> lock(currentMutex());

        // activity_suspend() có thể đã chạy (GUI thread) trong lúc ta đang chốt
        // record cũ ở trên: không mở lại record ngay sau khi máy khoá.
        if (g_suspended.load())
            return;

        startNewRecordLocked(&current);
        g_last_window = current;
    }
}

/*
 * Máy chuẩn bị khoá màn hình / đi ngủ: đặt cờ g_suspended (luồng theo dõi
 * dừng mở record mới), rồi chốt sổ record đang mở NGAY BÂY GIỜ - tại đúng
 * thời điểm khoá/ngủ, không phải đợi tới lúc mở khoá/thức dậy rồi mới tính
 * lùi.
 */
void activity_suspend(void)
{
    {
        // Đặt cờ trong lúc giữ khoá để nhất quán với các đoạn kiểm tra cờ
        // trong monitor_activity().
        std::lock_guard<std::mutex> lock(currentMutex());
        g_suspended.store(true);
    }

    closeCurrentSession();

    JIT_LOG(
        L"\n"
        L"=====================================\n"
        L"[LOCK/SLEEP] Da chot record hien tai,\n"
        L"tam dung theo doi cho toi khi co hoat\n"
        L"dong tro lai.\n"
        L"=====================================\n"
    );
}

/*
 * Máy mở khoá màn hình / thức dậy từ sleep: bỏ cờ g_suspended để
 * monitor_activity() tiếp tục - lần gọi kế tiếp sẽ mở 1 record mới nhờ
 * closeCurrentSession() đã reset trạng thái.
 */
void activity_resume(void)
{
    g_suspended.store(false);
    JIT_LOG(L"[LOCK/SLEEP] May da mo khoa / thuc day, tiep tuc theo doi hoat dong.\n");
}

int activity_check_limits(
    ActivityLimitEvent* events_out,
    int max_events)
{
    int eventCount = 0;

    AppSettings s;
    settings_get(&s);

    /*
     * Chỉ máy con (agent) mới tự thực thi giới hạn - máy phụ huynh
     * không có gì để "tự chặn" cả.
     */
    if (s.app_role != APP_ROLE_CHILD)
        return 0;

    if (!auth_is_logged_in())
        return 0;

    // Đọc từ bộ nhớ đệm cục bộ (không gọi mạng) - xem applimits.h.
    AppLimit limits[MAX_LIMITS];
    int limitCount = applimits_get_my_limits(limits, MAX_LIMITS);

    if (limitCount <= 0)
        return 0;

    wchar_t currentProcess[MAX_PATH] = {0};
    time_t currentStartTime;

    {
        std::lock_guard<std::mutex> lock(currentMutex());
        wcscpy_s(currentProcess, MAX_PATH, g_current_record.process_name);
        currentStartTime = g_current_record.start_time;
    }

    if (currentProcess[0] == L'\0')
        return 0;

    char currentProcessUtf8[512] = {0};
    WideCharToMultiByte(
        CP_UTF8, 0, currentProcess, -1,
        currentProcessUtf8, sizeof(currentProcessUtf8), NULL, NULL
    );

    for (int i = 0; i < limitCount; i++)
    {
        if (_stricmp(limits[i].process_name, currentProcessUtf8) != 0)
            continue;

        int shouldBlock = 0;
        int reason = 0;

        if (limits[i].blocked)
        {
            shouldBlock = 1;
            reason = LIMIT_REASON_BLOCKED;
        }
        else if (limits[i].daily_limit_sec >= 0)
        {
            /*
             * Cộng thêm thời gian của phiên đang mở (nếu đúng là
             * process đang bị giới hạn và bắt đầu trong hôm nay) vào
             * db_get_today_seconds() (chỉ cộng các bản ghi ĐÃ đóng,
             * phiên đang mở không tính vào cho tới khi nó tự đóng).
             */
            long usedToday = db_get_today_seconds(currentProcess);

            if (currentStartTime > 0)
            {
                time_t now = time(NULL);
                struct tm todayTm;
                localtime_s(&todayTm, &now);
                todayTm.tm_hour = 0;
                todayTm.tm_min = 0;
                todayTm.tm_sec = 0;
                time_t dayStart = mktime(&todayTm);

                time_t openSessionStart = (currentStartTime > dayStart) ? currentStartTime : dayStart;

                if (now > openSessionStart)
                    usedToday += static_cast<long>(now - openSessionStart);
            }

            if (usedToday >= limits[i].daily_limit_sec)
            {
                shouldBlock = 1;
                reason = LIMIT_REASON_TIME_UP;
            }
        }

        if (!shouldBlock)
            break;

        /*
         * Chốt sổ record đang mở NGAY BÂY GIỜ (dùng chung logic với
         * activity_suspend() để không lặp code và không tính nhầm thời
         * gian sau khi app đã bị kill vào thời lượng sử dụng). KHÔNG dùng
         * chính activity_suspend(): hàm đó còn đặt cờ g_suspended (dừng
         * theo dõi cho tới khi mở khoá) - ở đây ta chỉ muốn chốt sổ.
         */
        closeCurrentSession();

        if (killIfForeground(currentProcess))
        {
            JIT_LOG(
                L"\n=====================================\n"
                L"[LIMIT] Da chan app theo yeu cau cua phu huynh: %ls\n"
                L"=====================================\n",
                currentProcess
            );

            if (eventCount < max_events)
            {
                wcscpy_s(events_out[eventCount].process_name, MAX_PATH, currentProcess);
                events_out[eventCount].reason = reason;
                eventCount++;
            }
        }

        break; /* chỉ có 1 process đang active tại 1 thời điểm */
    }

    return eventCount;
}
