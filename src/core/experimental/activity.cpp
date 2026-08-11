//
// activity.cpp
//
// CHUYỂN TỪ C SANG C++ (bản EXPERIMENTAL) - giữ nguyên interface
// extern "C" trong activity.h. Đây là module NHẠY CẢM NHẤT về đồng
// bộ hoá luồng (worker thread ghi, GUI thread + HTTP thread đọc), nên
// khi chuyển đổi mình giữ NGUYÊN VẸN từng ranh giới khoá/mở khoá y
// hệt bản STABLE - không gộp, không tách thêm, không đổi thời điểm
// lock được giữ hay nhả (đặc biệt là finish_current_record(), nơi
// lock CHỦ Ý được nhả trước khi gọi settings_get()/db_insert_activity()
// để tránh giữ khoá trong lúc I/O). Chỉ đổi:
//   - std::mutex (magic static) + std::lock_guard thay
//     CRITICAL_SECTION tự quản - cùng lý do đã nêu ở device.cpp.
//     std::lock_guard tự nhả khoá khi ra khỏi scope, nên những đoạn
//     bản C cũ phải tự EnterCriticalSection/LeaveCriticalSection lặp
//     lại 2-3 lần trong 1 hàm giờ dùng { } để giới hạn phạm vi mỗi
//     lock_guard, rõ ràng hơn về việc khoá đang giữ ở đoạn nào.
//   - g_finish_in_progress vẫn là 1 biến int thường (được bảo vệ bởi
//     CÙNG mutex, giống hệt bản C) - không đổi sang std::atomic vì
//     bản C cũng không dùng atomic cho biến này (nó luôn được đọc/ghi
//     trong lúc đã giữ khoá), đổi sẽ là thay đổi hành vi không cần
//     thiết.
//

#include "activity.h"
#include "database.h"
#include "settings.h"
#include "applimits.h"
#include "auth.h"

#include <windows.h>
#include <psapi.h>

#include <cstdio>
#include <cwchar>
#include <ctime>
#include <cstring>
#include <mutex>

namespace {

ActiveWindow g_last_window = {};
ActivityRecord g_current_record = {};
bool g_finish_in_progress = false;

std::mutex& currentMutex()
{
    static std::mutex m;
    return m;
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

    HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (!process)
        return 0;

    int success = GetModuleBaseNameW(process, NULL, window->process_name, MAX_PATH);
    CloseHandle(process);

    return success;
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

    wprintf(
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
 * Bắt đầu activity mới
 */
void startNewRecord(const ActiveWindow* window)
{
    std::lock_guard<std::mutex> lock(currentMutex());

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

        g_finish_in_progress = true;

        g_current_record.end_time = time(NULL);
        g_current_record.duration_seconds =
            static_cast<long>(g_current_record.end_time - g_current_record.start_time);

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
    //     db_insert_activity bên dưới), y hệt bản STABLE.

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
        PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_TERMINATE,
        FALSE, pid
    );
    if (!process)
        return 0;

    wchar_t currentName[MAX_PATH] = {0};
    int gotName = GetModuleBaseNameW(process, NULL, currentName, MAX_PATH);

    int killed = 0;
    if (gotName && _wcsicmp(currentName, processName) == 0)
    {
        TerminateProcess(process, 1);
        killed = 1;
    }

    CloseHandle(process);
    return killed;
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
    ActiveWindow current = {};

    if (!getActiveWindowInfo(&current))
        return;

    bool firstRun;
    bool processChanged;
    bool titleChanged;

    {
        std::lock_guard<std::mutex> lock(currentMutex());

        firstRun = (g_last_window.process_name[0] == L'\0');
        processChanged = !firstRun && wcscmp(current.process_name, g_last_window.process_name) != 0;
        titleChanged = !firstRun && wcscmp(current.window_title, g_last_window.window_title) != 0;
    }

    /*
     * Lần chạy đầu tiên (bao gồm cả lần đầu tiên sau khi
     * activity_suspend() đã reset trạng thái vì máy vừa khoá màn
     * hình/ngủ) - luôn bắt đầu 1 record mới.
     */
    if (firstRun)
    {
        {
            std::lock_guard<std::mutex> lock(currentMutex());
            g_last_window = current;
        }

        startNewRecord(&current);

        wprintf(L"[START] %ls\n", current.process_name);
        return;
    }

    /* Nếu không thay đổi thì bỏ qua */
    if (!processChanged && !titleChanged)
        return;

    /* Kết thúc record cũ */
    finishCurrentRecord();

    /* Log chuyển app */
    wprintf(
        L"\n"
        L"=====================================\n"
        L"[SWITCH]\n"
        L"Process : %ls\n"
        L"Title   : %ls\n"
        L"=====================================\n",
        current.process_name, current.window_title
    );

    /* Record mới */
    startNewRecord(&current);

    {
        std::lock_guard<std::mutex> lock(currentMutex());
        g_last_window = current;
    }
}

/*
 * Máy chuẩn bị khoá màn hình / đi ngủ: chốt sổ record đang mở NGAY
 * BÂY GIỜ (tại đúng thời điểm khoá/ngủ, không phải đợi tới lúc mở
 * khoá/thức dậy rồi mới tính lùi), sau đó reset g_last_window về
 * rỗng.
 *
 * Việc reset này là mấu chốt: nó buộc lần gọi monitor_activity() kế
 * tiếp - dù có xảy ra sau vài giây hay vài giờ, dù cửa sổ active lúc
 * đó có trùng y hệt cửa sổ trước khi khoá máy hay không - luôn rơi
 * vào nhánh "first_run", tức luôn bắt đầu MỘT RECORD HOÀN TOÀN MỚI
 * thay vì lặng lẽ cộng dồn khoảng thời gian khoá máy/ngủ vào record
 * cũ.
 */
void activity_suspend(void)
{
    bool hasOpenRecord;
    {
        std::lock_guard<std::mutex> lock(currentMutex());
        hasOpenRecord = (g_last_window.process_name[0] != L'\0');
    }

    if (!hasOpenRecord)
    {
        /*
         * App vừa khởi động đã bị khoá máy ngay, chưa kịp có record
         * nào để chốt - không có gì để làm.
         */
        return;
    }

    finishCurrentRecord();

    {
        std::lock_guard<std::mutex> lock(currentMutex());
        memset(&g_last_window, 0, sizeof(g_last_window));
    }

    wprintf(
        L"\n"
        L"=====================================\n"
        L"[LOCK/SLEEP] Da chot record hien tai,\n"
        L"tam dung theo doi cho toi khi co hoat\n"
        L"dong tro lai.\n"
        L"=====================================\n"
    );
}

/*
 * Máy mở khoá màn hình / thức dậy từ sleep. Không cần làm gì thêm để
 * logic đúng - monitor_activity() sẽ tự bắt đầu 1 record mới ở lần
 * gọi tiếp theo, nhờ g_last_window đã được activity_suspend() reset
 * về rỗng. Hàm này chủ yếu để log rõ thời điểm resume và dự phòng mở
 * rộng sau này.
 */
void activity_resume(void)
{
    wprintf(L"[LOCK/SLEEP] May da mo khoa / thuc day, tiep tuc theo doi hoat dong.\n");
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
             * db_get_today_seconds() - xem ghi chú đầy đủ trong bản
             * STABLE (src/core/stable/activity.c) về lý do fix này
             * tồn tại (db_get_today_seconds() chỉ cộng các bản ghi ĐÃ
             * đóng, phiên đang mở không tính vào cho tới khi nó tự
             * đóng).
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
         * Chốt sổ record đang mở NGAY BÂY GIỜ (giống hệt
         * activity_suspend() - dùng lại toàn bộ logic đó để không
         * lặp code và không tính nhầm thời gian sau khi app đã bị
         * kill vào thời lượng sử dụng).
         */
        activity_suspend();

        if (killIfForeground(currentProcess))
        {
            wprintf(
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
