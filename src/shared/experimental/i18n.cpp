//
// i18n.cpp
//
// CHUYỂN TỪ C SANG C++ (bản EXPERIMENTAL) - giữ nguyên interface
// extern "C" trong i18n.h. BẢNG DỊCH GIỮ NGUYÊN 100% (copy y hệt từ
// bản STABLE, không dịch lại/không đổi bất kỳ chuỗi nào) - chỉ khác
// CÁCH TRA CỨU:
//   - Bản STABLE: quét tuyến tính (O(n)) qua mảng g_entries mỗi lần
//     gọi i18n_t() - với ~150 dòng dịch, không chậm tới mức đáng lo,
//     nhưng vẫn là chỗ có thể cải thiện rõ ràng.
//   - Bản EXPERIMENTAL: dựng 1 std::unordered_map<string,Entry> MỘT
//     LẦN DUY NHẤT (magic static, an toàn luồng) từ chính mảng dữ
//     liệu này, tra cứu sau đó là O(1) trung bình.
//

#include "i18n.h"
#include "settings.h"

#include <cstring>
#include <string>
#include <unordered_map>

namespace {

struct I18nEntry
{
    const char* key;
    const char* en;
    const char* vi;
};

const I18nEntry g_entries[] = {


    /* ---- Tray menu ---- */
    {"tray.login",              "Log in / Sign up...",               "Đăng nhập / Đăng ký..."},
    {"tray.logout",              "Log out",                           "Đăng xuất"},
    {"tray.pause",               "Pause tracking",                    "Tạm dừng theo dõi"},
    {"tray.resume",              "Resume tracking",                   "Tiếp tục theo dõi"},
    {"tray.report",               "View today's report",               "Xem báo cáo hôm nay"},
    {"tray.settings",            "Settings...",                        "Cài đặt..."},
    {"tray.supabase_setup",      "Supabase Setup...",                  "Thiết lập Supabase..."},
    {"tray.remote_view",         "Remote View...",                     "Xem từ xa..."},
    {"tray.dashboards_menu",     "Dashboards",                         "Bảng điều khiển"},
    {"tray.dashboard_python",    "Open Web Dashboard (Python)",        "Mở Dashboard Web (Python)"},
    {"tray.dashboard_go",        "Open Local Dashboard (Go)",          "Mở Dashboard Cục Bộ (Go)"},
    {"tray.dashboard_tip",
        "Both dashboards show the same data on http://127.0.0.1:5000 "
        "(they're two implementations of the same dashboard, not meant "
        "to run at the same time). Whichever is already running will "
        "just be opened again.",
        "Cả hai dashboard đều hiển thị cùng dữ liệu tại "
        "http://127.0.0.1:5000 (đây là 2 cách triển khai của cùng 1 "
        "tính năng, không nhằm chạy song song). Cái nào đang chạy sẵn "
        "sẽ chỉ được mở lại, không chạy chồng lên."},
    {"tray.parent_link",        "Monitored By...",                    "Được Giám Sát Bởi..."},
    {"tray.parent_link_tip",
        "See the full list of parent accounts requesting or already "
        "approved to view this device's activity, and revoke access "
        "at any time.",
        "Xem đầy đủ danh sách tài khoản phụ huynh đang xin/đã được xem "
        "hoạt động của máy này, và thu hồi quyền xem bất kỳ lúc nào."},
    {"tray.parent_dashboard",   "Parent Dashboard...",                 "Bảng Điều Khiển Phụ Huynh..."},
    {"tray.parent_dashboard_tip",
        "Invite a child, view their activity, and set time limits/blocked apps.",
        "Mời con, xem hoạt động, và đặt giới hạn thời gian/chặn app."},
    {"tray.debug_console",       "Show debug console",                 "Hiện cửa sổ debug"},
    {"tray.check_updates",      "Check for Updates...",               "Kiểm tra cập nhật..."},
    {"tray.checking_updates",   "Checking for updates...",             "Đang kiểm tra cập nhật..."},
    {"tray.about",                "About JustInTime...",                "Giới thiệu JustInTime..."},
    {"tray.exit",                 "Exit",                                "Thoát"},
    {"tray.update_available",   "\xE2\xAC\x86 Update available: v%1", "\xE2\xAC\x86 Có bản cập nhật: v%1"},
    {"tray.logged_in_as",        "Logged in: %1",                       "Đã đăng nhập: %1"},

    /* ---- About dialog ---- */
    {"about.title",               "About JustInTime",                    "Giới thiệu JustInTime"},
    {"about.body",
        "<h3>JustInTime</h3>"
        "<p>Version %1</p>"
        "<p>Lightweight activity tracker with optional Supabase cloud sync, "
        "a local/offline dashboard, and consent-based parent/child linking.</p>"
        "<p>Publisher: %2<br>"
        "<a href=\"%3\">%3</a></p>",
        "<h3>JustInTime</h3>"
        "<p>Phiên bản %1</p>"
        "<p>Công cụ theo dõi hoạt động máy tính gọn nhẹ, đồng bộ cloud tuỳ "
        "chọn qua Supabase, dashboard cục bộ/offline, và liên kết phụ "
        "huynh-con có đồng thuận.</p>"
        "<p>Nhà phát hành: %2<br>"
        "<a href=\"%3\">%3</a></p>"},

    /* ---- Update notifications ---- */
    {"update.you_have_latest",    "You're running the latest version (%1).",
                                    "Bạn đang dùng phiên bản mới nhất (%1)."},
    {"update.check_failed",       "Could not check for updates:\n%1",
                                    "Không thể kiểm tra cập nhật:\n%1"},

    /* ---- Limit-blocked tray notifications ---- */
    {"limit.blocked",
        "\"%1\" has been closed - a parent has blocked this app.",
        "\"%1\" đã bị đóng - phụ huynh đã chặn ứng dụng này."},
    {"limit.time_up",
        "\"%1\" has been closed - today's time limit set by a parent has been reached.",
        "\"%1\" đã bị đóng - đã hết thời gian sử dụng hôm nay do phụ huynh đặt."},
    {"limit.generic",
        "\"%1\" has been closed by a parent-set limit.",
        "\"%1\" đã bị đóng do giới hạn phụ huynh đặt."},

    /* ---- Settings dialog: role + language ---- */
    {"settings.role_label",      "This machine's role:",               "Vai trò máy này:"},
    {"settings.role_child",
        "Child (Agent) - this machine is tracked/limited",
        "Con (Agent) - máy này được theo dõi/giới hạn"},
    {"settings.role_parent",
        "Parent - this machine views/limits child machines",
        "Phụ huynh (Parent) - máy này xem/giới hạn máy con"},
    {"settings.role_note",
        "Changing role does not delete data already tracked on this "
        "machine. \"Child\" is the default and keeps the old behavior.",
        "Đổi vai trò không xoá dữ liệu đã theo dõi trên máy này. Vai trò "
        "\"Con\" là mặc định và giữ nguyên hành vi cũ."},
    {"settings.language_label",  "Interface language:",                 "Ngôn ngữ giao diện:"},
    {"settings.page_title",      "Settings",                            "Cài đặt"},
    {"page.save_btn",            "Save",                                "Lưu"},

    /* ---- ParentLinkPage (child transparency view) ---- */
    {"plink.title",             "Monitored By...",                    "Được giám sát bởi..."},
    {"plink.note",
        "The list below shows EVERY parent account requesting or already "
        "approved to view this device's activity, including pending "
        "invites. You can revoke access at any time.",
        "Danh sách dưới đây liệt kê ĐẦY ĐỦ mọi tài khoản phụ huynh đang "
        "xin hoặc đã được phép xem hoạt động của máy này, kể cả các lời "
        "mời đang chờ bạn đồng ý. Bạn có thể thu hồi quyền xem bất kỳ "
        "lúc nào."},
    {"plink.col_email",          "Parent email",                        "Email phụ huynh"},
    {"plink.col_status",         "Status",                              "Trạng thái"},
    {"plink.approve_btn",       "Approve",                              "Đồng ý (Approve)"},
    {"plink.revoke_btn",        "Revoke",                               "Thu hồi (Revoke)"},
    {"plink.close_btn",          "Close",                               "Đóng"},
    {"plink.status_pending",    "Waiting for your approval",           "Đang chờ bạn đồng ý"},
    {"plink.status_approved",   "Approved - currently monitoring",     "Đã đồng ý - đang được xem"},
    {"plink.status_revoked",    "Revoked",                              "Đã thu hồi"},
    {"plink.empty",
        "No parent accounts linked to this device yet.",
        "Chưa có phụ huynh nào liên kết với tài khoản này."},
    {"plink.select_row",         "Select a row in the list first.",     "Chọn 1 dòng trong danh sách trước."},
    {"plink.approved_msg",
        "This parent can now view this device's activity.",
        "Đã đồng ý cho phụ huynh này xem hoạt động của máy."},
    {"plink.revoke_confirm",
        "Revoke this parent's access? They will no longer be able to view this device's activity.",
        "Thu hồi quyền xem của phụ huynh này? Họ sẽ không còn xem được hoạt động của máy nữa."},
    {"plink.revoked_msg",       "Revoked.",                             "Đã thu hồi."},

    /* ---- ParentPage (parent role) ---- */
    {"pdlg.title",                "Parent Dashboard",                    "Parent Dashboard"},
    {"pdlg.invite_box",          "Invite a New Child",                  "Mời con mới"},
    {"pdlg.invite_note",
        "Enter your child's JustInTime account email. They will see this "
        "invite under \"Monitored By...\" and must approve it before you "
        "can see their activity.",
        "Nhập email tài khoản JustInTime của con. Con sẽ thấy lời mời này "
        "trong mục \"Được giám sát bởi...\" và phải chủ động đồng ý thì "
        "bạn mới xem được hoạt động của con."},
    {"pdlg.invite_placeholder",  "child-email@example.com",              "email-cua-con@example.com"},
    {"pdlg.invite_btn",           "Send Invite",                          "Gửi lời mời"},
    {"pdlg.children_box",        "Linked Children",                      "Các con đã liên kết"},
    {"pdlg.col_email",            "Child email",                         "Email con"},
    {"pdlg.col_status",           "Status",                              "Trạng thái"},
    {"pdlg.status_pending",      "Waiting for approval",                "Đang chờ con đồng ý"},
    {"pdlg.status_approved",     "Approved - visible",                  "Đã đồng ý - đang xem được"},
    {"pdlg.status_revoked",      "Revoked",                              "Đã bị thu hồi"},
    {"pdlg.children_empty",
        "No children invited yet. Use the box above to send your first invite.",
        "Chưa mời con nào. Dùng ô phía trên để gửi lời mời đầu tiên."},
    {"pdlg.limits_box",          "App Limits",                           "Giới hạn ứng dụng"},
    {"pdlg.limits_hint_select",
        "Select an approved child above to view/set limits.",
        "Chọn 1 con đã \"Đã đồng ý\" ở trên để xem/đặt giới hạn."},
    {"pdlg.limits_hint_count",
        "%1 limit(s) currently applied. Limits won't load/save if the child hasn't approved yet (\"pending\").",
        "%1 giới hạn đang áp dụng. Nếu con chưa đồng ý (\"pending\"), giới hạn sẽ không tải/lưu được."},
    {"pdlg.limits_none",
        "No limits set for this child yet. Fill the form below to add one.",
        "Chưa có giới hạn nào cho con này. Điền form bên dưới để thêm."},
    {"pdlg.limit_process",       "Process name:",                        "Tên tiến trình:"},
    {"pdlg.limit_process_placeholder", "e.g. RobloxPlayerBeta.exe",       "vd: RobloxPlayerBeta.exe"},
    {"pdlg.limit_amount",         "Limit:",                               "Giới hạn:"},
    {"pdlg.limit_minutes_suffix", " min/day",                             " phút/ngày"},
    {"pdlg.limit_nolimit",       "No time limit",                        "Không giới hạn thời gian"},
    {"pdlg.limit_block",          "Block entirely (app can't be opened)", "Chặn hẳn (không cho mở app này)"},
    {"pdlg.limit_save_btn",      "Save Limit",                           "Lưu giới hạn"},
    {"pdlg.limit_delete_btn",    "Delete Selected Limit",                "Xoá giới hạn đã chọn"},
    {"pdlg.limit_blocked_label", "-",                                      "-"},
    {"pdlg.limit_unlimited",     "No limit",                              "Không giới hạn"},
    {"pdlg.limit_allowed",        "Allowed (limited)",                    "Cho phép (có giới hạn)"},
    {"pdlg.close_btn",            "Close",                                "Đóng"},
    {"pdlg.select_child_first",  "Select a child in the list above first.", "Chọn 1 con trong danh sách phía trên trước."},
    {"pdlg.enter_process",       "Enter a process name (e.g. RobloxPlayerBeta.exe).", "Nhập tên tiến trình (vd: RobloxPlayerBeta.exe)."},
    {"pdlg.invite_sent",
        "Invite sent. The child needs to open \"Monitored By...\" on their device to approve it.",
        "Đã gửi lời mời. Con cần vào mục \"Được giám sát bởi...\" trên máy của con để đồng ý."},
    {"pdlg.limit_saved",          "Limit saved.",                         "Đã lưu giới hạn."},
    {"pdlg.enter_child_email",   "Enter your child's email first.",     "Nhập email của con trước."},
    {"pdlg.select_limit_first",  "Select a limit in the list first.",   "Chọn 1 giới hạn trong danh sách trước."},

    /* ---- Control Panel (cửa sổ điều khiển hợp nhất) ---- */
    {"cp.title",              "JustInTime Control Panel",           "Bảng điều khiển JustInTime"},
    {"cp.nav_overview",       "Overview",                           "Tổng quan"},
    {"cp.nav_account",        "Account",                            "Tài khoản"},
    {"cp.nav_settings",       "Settings",                           "Cài đặt"},
    {"cp.nav_remoteview",     "Remote View",                        "Xem từ xa"},
    {"cp.nav_parentlink",     "Monitored By",                       "Được giám sát bởi"},
    {"cp.nav_family",         "Family",                             "Gia đình"},
    {"cp.nav_advanced",       "Advanced",                           "Nâng cao"},
    {"cp.nav_about",          "About",                              "Giới thiệu"},
    {"cp.nav_development",    "Development",                        "Phát triển"},

    {"cp.overview_greeting_in",
        "Welcome back",
        "Chào mừng trở lại"},
    {"cp.overview_greeting_out",
        "Welcome to JustInTime",
        "Chào mừng đến với JustInTime"},
    {"cp.overview_not_logged_in",
        "You're not logged in yet. Your activity is still tracked and saved "
        "locally on this computer - logging in just adds cloud sync, "
        "family features, and Remote View.",
        "Bạn chưa đăng nhập. Hoạt động vẫn được theo dõi và lưu cục bộ trên "
        "máy này bình thường - đăng nhập chỉ để thêm đồng bộ đám mây, "
        "tính năng gia đình, và Xem từ xa."},
    {"cp.overview_login_cta",     "Log in / Sign up",                  "Đăng nhập / Đăng ký"},
    {"cp.overview_status_tracking", "Tracking your activity right now.", "Đang theo dõi hoạt động."},
    {"cp.overview_status_paused",   "Tracking is paused.",               "Đang tạm dừng theo dõi."},
    {"cp.overview_pause_btn",       "Pause tracking",                    "Tạm dừng theo dõi"},
    {"cp.overview_resume_btn",      "Resume tracking",                   "Tiếp tục theo dõi"},
    {"cp.overview_today_title",     "Today so far",                      "Hôm nay"},
    {"cp.overview_quick_links",     "Quick links",                       "Lối tắt nhanh"},
    {"cp.overview_open_dashboard",  "Open full Dashboard",               "Mở Dashboard đầy đủ"},
    {"cp.overview_first_run_title",
        "First time here?",
        "Lần đầu dùng?"},
    {"cp.overview_first_run_body",
        "JustInTime quietly tracks which apps and windows you use, so you "
        "(or, if you choose, a linked family member) can see how time is "
        "spent. Nothing is hidden: check \"Monitored By\" any time to see "
        "exactly who can see this computer's activity, and revoke access "
        "whenever you want.",
        "JustInTime âm thầm ghi lại app/cửa sổ bạn dùng, để bạn (hoặc, nếu "
        "bạn chọn, một thành viên gia đình đã liên kết) xem được thời gian "
        "dùng máy. Không có gì bị giấu: xem mục \"Được giám sát bởi\" bất "
        "cứ lúc nào để biết chính xác ai đang xem được hoạt động của máy "
        "này, và thu hồi quyền xem bất cứ khi nào bạn muốn."},
    {"cp.overview_no_data",       "No activity recorded yet today.",   "Chưa có hoạt động nào được ghi lại hôm nay."},
    {"cp.overview_machines_title",       "Machines",                            "Các máy"},
    {"cp.overview_machines_this_device", "(this device)",                       "(máy này)"},
    {"cp.overview_machines_login_required",
        "Log in to see every device linked to your account.",
        "Đăng nhập để xem mọi thiết bị liên kết với tài khoản của bạn."},
    {"cp.overview_machines_none", "No other devices have reported in yet.", "Chưa có thiết bị nào khác gửi tín hiệu."},
    {"cp.overview_dashboard_missing",
        "Could not find the web dashboard (%1). Make sure JustInTime Dashboard is installed in the same folder as this app.",
        "Không tìm thấy dashboard web (%1). Hãy chắc chắn JustInTime Dashboard đã được cài cùng thư mục với app này."},

    /* ---- LoginPage (trang "Account") ---- */
    {"login.heading",            "Account",                             "Tài khoản"},
    {"login.note",
        "Log in to sync your activity to the cloud, link with a parent "
        "account, or use Remote View / device messaging.",
        "Đăng nhập để đồng bộ hoạt động lên đám mây, liên kết với tài "
        "khoản phụ huynh, hoặc dùng Xem từ xa / nhắn tin liên máy."},
    {"login.email_label",        "Email:",                              "Email:"},
    {"login.password_label",     "Password:",                           "Mật khẩu:"},
    {"login.login_btn",          "Log in",                              "Đăng nhập"},
    {"login.signup_btn",         "Sign up",                             "Đăng ký"},
    {"login.logout_btn",         "Log out",                             "Đăng xuất"},
    {"login.logged_in_as",
        "Logged in as <b>%1</b>.\n\nYour activity now syncs to the cloud.",
        "Đã đăng nhập với <b>%1</b>.\n\nHoạt động của bạn giờ đồng bộ lên đám mây."},
    {"login.enter_both",         "Please enter both email and password.", "Vui lòng nhập cả email và mật khẩu."},
    {"login.logout_confirm",     "Log out of the current account?",     "Đăng xuất khỏi tài khoản hiện tại?"},
    {"login.logout_done",
        "Logged out. Data is still saved locally, but cloud sync is paused.",
        "Đã đăng xuất. Dữ liệu vẫn được lưu cục bộ, nhưng đồng bộ đám mây tạm dừng."},
    {"login.success",
        "Logged in successfully! Your data will now sync to the cloud.",
        "Đăng nhập thành công! Dữ liệu của bạn giờ sẽ đồng bộ lên đám mây."},

    /* ---- RemoteViewPage ---- */
    {"rview.heading",            "Remote View",                         "Xem từ xa"},
    {"rview.enable_check",
        "Enable Remote View (read-only, HTTP, unencrypted)",
        "Bật Xem từ xa (chỉ đọc, HTTP, không mã hoá)"},
    {"rview.port_label",         "Port:",                               "Cổng:"},
    {"rview.token_label",        "Access token:",                       "Mã truy cập:"},
    {"rview.regenerate_btn",     "Regenerate",                          "Tạo mã mới"},
    {"rview.url_label",          "Status URL:",                         "URL trạng thái:"},
    {"rview.copy_btn",           "Copy status URL",                     "Sao chép URL"},
    {"rview.warning",
        "Warning: this is plain HTTP, not encrypted. Only use it on a "
        "trusted local network or VPN - do not expose this port directly "
        "to the public internet.",
        "Cảnh báo: đây là HTTP thường, không mã hoá. Chỉ nên dùng trong "
        "mạng LAN/VPN tin cậy - không mở cổng này thẳng ra Internet công cộng."},
    {"rview.save_btn",           "Save",                                "Lưu"},
    {"rview.copied",             "Copied to clipboard.",                "Đã sao chép."},
    {"rview.saved",              "Remote View settings saved.",         "Đã lưu cấu hình Xem từ xa."},

    /* ---- SupabaseSetupPage ---- */
    {"supabase.heading",         "Supabase Setup (Advanced)",           "Thiết lập Supabase (Nâng cao)"},
    {"supabase.note",
        "Only change this if you're running your own Supabase project "
        "instead of the default one. Leave both fields empty to use the "
        "built-in defaults.",
        "Chỉ đổi mục này nếu bạn tự chạy project Supabase riêng thay vì "
        "dùng mặc định. Để trống cả 2 ô để dùng giá trị mặc định sẵn có."},
    {"supabase.url_label",       "Supabase URL:",                       "Supabase URL:"},
    {"supabase.key_label",       "Publishable Key:",                    "Publishable Key:"},
    {"supabase.save_btn",        "Save",                                "Lưu"},
    {"supabase.saved",           "Supabase configuration saved.",       "Đã lưu cấu hình Supabase."},

    /* ---- Development page (build info - dành cho dev/người tò mò,
       KHÔNG phải cấu hình người dùng thường cần đụng vào) ---- */
    {"dev.subtitle",
        "Build/technical information - most people never need this page.",
        "Thông tin kỹ thuật/build - hầu hết mọi người không cần trang này."},
    {"dev.core_mode_title",   "Core implementation",                "Lõi đang dùng"},
    {"dev.badge_stable",      "STABLE (C)",                          "STABLE (C)"},
    {"dev.badge_experimental","EXPERIMENTAL (C++)",                  "EXPERIMENTAL (C++)"},
    {"dev.core_desc_stable",
        "This build uses the original, battle-tested C implementation "
        "for the core data/network modules. This is the recommended "
        "choice for everyday use.",
        "Bản build này dùng lõi C gốc, đã chạy ổn định lâu dài cho các "
        "module dữ liệu/mạng cốt lõi. Đây là lựa chọn khuyến nghị cho "
        "dùng hàng ngày."},
    {"dev.core_desc_experimental",
        "This build uses the newer C++ rewrite (RAII, std::string, "
        "std::mutex...) for the same modules. It hasn't run in the "
        "field long enough to be considered as proven as the Stable "
        "build yet - useful for development and comparison, not "
        "recommended as your only build.",
        "Bản build này dùng bản viết lại C++ mới hơn (RAII, "
        "std::string, std::mutex...) cho cùng các module. Chưa chạy đủ "
        "lâu ngoài thực tế để coi là ổn định như bản Stable - hữu ích "
        "để phát triển/so sánh, không khuyến nghị dùng làm bản duy "
        "nhất."},
    {"dev.switch_title",      "Switching modes",                     "Đổi chế độ"},
    {"dev.switch_body",
        "cmake -DJUSTINTIME_CORE=STABLE ..\ncmake -DJUSTINTIME_CORE=EXPERIMENTAL ..\n\n"
        "Chosen at build time only - rebuild the app to switch.",
        "cmake -DJUSTINTIME_CORE=STABLE ..\ncmake -DJUSTINTIME_CORE=EXPERIMENTAL ..\n\n"
        "Chỉ chọn được lúc build - phải build lại app để đổi."},

    {NULL, NULL, NULL}
};

const std::unordered_map<std::string, const I18nEntry*>& lookupTable()
{
    static const std::unordered_map<std::string, const I18nEntry*> table = []
    {
        std::unordered_map<std::string, const I18nEntry*> m;
        for (int i = 0; g_entries[i].key != nullptr; i++)
            m.emplace(g_entries[i].key, &g_entries[i]);
        return m;
    }();
    return table;
}

} // namespace

const char* i18n_t(const char* key)
{
    if (!key)
        return "";

    AppSettings s;
    settings_get(&s);

    const auto& table = lookupTable();
    const auto it = table.find(key);

    if (it == table.end())
        return key;

    return (s.language == APP_LANG_VI) ? it->second->vi : it->second->en;
}
