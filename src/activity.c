//
// Created by LoPhongCorporation on 6/24/2026.
//
#include "../include/activity.h"
#include "../include/database.h"
#include "../include/settings.h"

#include <windows.h>
#include <psapi.h>

#include <stdio.h>
#include <wchar.h>
#include <time.h>
#include <string.h>

static ActiveWindow g_last_window = {0};
static ActivityRecord g_current_record = {0};

static CRITICAL_SECTION g_current_lock;
static int g_current_lock_ready = 0;

static void ensure_current_lock(void)
{
    if (!g_current_lock_ready)
    {
        InitializeCriticalSection(&g_current_lock);
        g_current_lock_ready = 1;
    }
}

/*
 * Lấy snapshot activity đang diễn ra NGAY LÚC NÀY (dùng
 * cho remote view - xem real-time không qua cloud).
 * Thread-safe: có thể gọi từ thread khác (HTTP server thread)
 * trong khi worker thread vẫn đang cập nhật g_current_record.
 */
void activity_get_current(
    wchar_t* process_out, int process_size,
    wchar_t* title_out, int title_size,
    time_t* since_out)
{
    ensure_current_lock();

    EnterCriticalSection(&g_current_lock);

    if (process_out && process_size > 0)
        wcscpy_s(process_out, process_size, g_current_record.process_name);

    if (title_out && title_size > 0)
        wcscpy_s(title_out, title_size, g_current_record.window_title);

    if (since_out)
        *since_out = g_current_record.start_time;

    LeaveCriticalSection(&g_current_lock);
}

/*
 * Lấy thông tin cửa sổ hiện tại
 */
static int get_active_window_info(
    ActiveWindow* window)
{
    if (!window)
        return 0;

    HWND hwnd = GetForegroundWindow();

    if (!hwnd)
        return 0;

    GetWindowTextW(
        hwnd,
        window->window_title,
        sizeof(window->window_title) /
        sizeof(window->window_title[0])
    );

    DWORD pid = 0;

    GetWindowThreadProcessId(
        hwnd,
        &pid
    );

    HANDLE process = OpenProcess(
        PROCESS_QUERY_INFORMATION |
        PROCESS_VM_READ,
        FALSE,
        pid
    );

    if (!process)
        return 0;

    int success = GetModuleBaseNameW(
        process,
        NULL,
        window->process_name,
        MAX_PATH
    );

    CloseHandle(process);

    return success;
}

/*
 * In activity vừa hoàn thành
 */
static void print_activity_report(
    const ActivityRecord* record)
{
    long total =
        record->duration_seconds;

    long hours =
        total / 3600;

    long minutes =
        (total % 3600) / 60;

    long seconds =
        total % 60;

    wprintf(
        L"\n"
        L"=====================================\n"
        L"[ACTIVITY]\n"
        L"Process : %ls\n"
        L"Title   : %ls\n"
        L"Used    : %02ld:%02ld:%02ld\n"
        L"=====================================\n",
        record->process_name,
        record->window_title,
        hours,
        minutes,
        seconds
    );
}

/*
 * Bắt đầu activity mới
 */
static void start_new_record(
    const ActiveWindow* window)
{
    ensure_current_lock();
    EnterCriticalSection(&g_current_lock);

    wcscpy_s(
        g_current_record.process_name,
        MAX_PATH,
        window->process_name
    );

    wcscpy_s(
        g_current_record.window_title,
        512,
        window->window_title
    );

    g_current_record.start_time =
        time(NULL);

    g_current_record.end_time = 0;

    g_current_record.duration_seconds = 0;

    g_current_record.synced = 0;

    LeaveCriticalSection(&g_current_lock);
}

static int g_finish_in_progress = 0;

/*
 * Kết thúc activity hiện tại.
 *
 * Có "chốt khoá kép" (g_finish_in_progress) vì giờ đây hàm này
 * có thể được gọi từ 2 nguồn khác nhau gần như đồng thời:
 *   - worker thread, khi phát hiện đổi cửa sổ (monitor_activity)
 *   - GUI thread, khi bắt được sự kiện khoá máy/sleep (activity_suspend,
 *     gọi từ main.cpp)
 * Nếu cả 2 xảy ra cùng lúc (vd khoá máy đúng khoảnh khắc đổi app),
 * không có chốt này thì record hiện tại có thể bị insert 2 lần
 * vào DB. Chốt bằng cờ đơn giản trong critical section: lời gọi
 * thứ 2 tới trong lúc lời gọi thứ 1 chưa xong sẽ tự bỏ qua.
 */
static void finish_current_record(void)
{
    ensure_current_lock();
    EnterCriticalSection(&g_current_lock);

    if (g_finish_in_progress)
    {
        LeaveCriticalSection(&g_current_lock);
        return;
    }

    g_finish_in_progress = 1;

    g_current_record.end_time =
        time(NULL);

    g_current_record.duration_seconds =
        (long)(
            g_current_record.end_time -
            g_current_record.start_time
        );

    /*
     * Chụp lại 1 bản snapshot cục bộ để dùng sau khi rời khỏi
     * critical section - tránh trường hợp start_new_record()
     * (từ 1 lời gọi khác) ghi đè g_current_record ngay trong
     * lúc ta đang exclusion-check/print/insert bên dưới.
     */
    ActivityRecord record_to_save = g_current_record;

    LeaveCriticalSection(&g_current_lock);

    do
    {
        /*
         * Bỏ qua app nằm trong danh sách loại trừ (cấu hình
         * trong menu Cài đặt của tray).
         */
        if (
            settings_is_process_excluded(
                record_to_save.process_name
            )
        )
            break;

        /*
         * Bỏ qua record quá ngắn (ngưỡng lấy từ settings,
         * mặc định 2 giây).
         */
        AppSettings s;
        settings_get(&s);

        if (record_to_save.duration_seconds < s.min_duration_sec)
            break;

        print_activity_report(
            &record_to_save
        );

        db_insert_activity(
            &record_to_save
        );
    }
    while (0);

    EnterCriticalSection(&g_current_lock);
    g_finish_in_progress = 0;
    LeaveCriticalSection(&g_current_lock);
}

/*
 * Theo dõi activity
 */
void monitor_activity(void)
{
    ActiveWindow current = {0};

    if (!get_active_window_info(
            &current))
    {
        return;
    }

    ensure_current_lock();
    EnterCriticalSection(&g_current_lock);

    int first_run =
        g_last_window.process_name[0]
        == L'\0';

    int process_changed =
        !first_run &&
        wcscmp(
            current.process_name,
            g_last_window.process_name
        ) != 0;

    int title_changed =
        !first_run &&
        wcscmp(
            current.window_title,
            g_last_window.window_title
        ) != 0;

    LeaveCriticalSection(&g_current_lock);

    /*
     * Lần chạy đầu tiên (bao gồm cả lần đầu tiên sau khi
     * activity_suspend() đã reset trạng thái vì máy vừa
     * khoá màn hình/ngủ) - luôn bắt đầu 1 record mới.
     */
    if (first_run)
    {
        EnterCriticalSection(&g_current_lock);
        g_last_window = current;
        LeaveCriticalSection(&g_current_lock);

        start_new_record(
            &current
        );

        wprintf(
            L"[START] %ls\n",
            current.process_name
        );

        return;
    }

    /*
     * Nếu không thay đổi thì bỏ qua
     */
    if (
        !process_changed &&
        !title_changed
    )
    {
        return;
    }

    /*
     * Kết thúc record cũ
     */
    finish_current_record();

    /*
     * Log chuyển app
     */
    wprintf(
        L"\n"
        L"=====================================\n"
        L"[SWITCH]\n"
        L"Process : %ls\n"
        L"Title   : %ls\n"
        L"=====================================\n",
        current.process_name,
        current.window_title
    );

    /*
     * Record mới
     */
    start_new_record(
        &current
    );

    EnterCriticalSection(&g_current_lock);
    g_last_window = current;
    LeaveCriticalSection(&g_current_lock);
}

/*
 * Máy chuẩn bị khoá màn hình / đi ngủ: chốt sổ record đang mở
 * NGAY BÂY GIỜ (tại đúng thời điểm khoá/ngủ, không phải đợi
 * tới lúc mở khoá/thức dậy rồi mới tính lùi), sau đó reset
 * g_last_window về rỗng.
 *
 * Việc reset này là mấu chốt: nó buộc lần gọi monitor_activity()
 * kế tiếp - dù có xảy ra sau vài giây hay vài giờ, dù cửa sổ
 * active lúc đó có trùng y hệt cửa sổ trước khi khoá máy hay
 * không - luôn rơi vào nhánh "first_run", tức luôn bắt đầu MỘT
 * RECORD HOÀN TOÀN MỚI thay vì lặng lẽ cộng dồn khoảng thời
 * gian khoá máy/ngủ vào record cũ.
 */
void activity_suspend(void)
{
    ensure_current_lock();

    EnterCriticalSection(&g_current_lock);
    int has_open_record = g_last_window.process_name[0] != L'\0';
    LeaveCriticalSection(&g_current_lock);

    if (!has_open_record)
    {
        /*
         * App vừa khởi động đã bị khoá máy ngay, chưa kịp
         * có record nào để chốt - không có gì để làm.
         */
        return;
    }

    finish_current_record();

    EnterCriticalSection(&g_current_lock);
    memset(&g_last_window, 0, sizeof(g_last_window));
    LeaveCriticalSection(&g_current_lock);

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
 * Máy mở khoá màn hình / thức dậy từ sleep. Không cần làm gì
 * thêm để logic đúng - monitor_activity() sẽ tự bắt đầu 1
 * record mới ở lần gọi tiếp theo, nhờ g_last_window đã được
 * activity_suspend() reset về rỗng. Hàm này chủ yếu để log rõ
 * thời điểm resume và dự phòng mở rộng sau này.
 */
void activity_resume(void)
{
    wprintf(
        L"[LOCK/SLEEP] May da mo khoa / thuc day,"
        L" tiep tuc theo doi hoat dong.\n"
    );
}