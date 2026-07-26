//
// error_codes.h
// Mã lỗi dùng chung toàn app, giúp dễ báo lỗi/debug.
// Quy ước: E1xxx = Đăng nhập/Tài khoản, E2xxx = Database,
// E3xxx = Đồng bộ/Mạng, E4xxx = Cài đặt, E5xxx = Giao diện.
//

#ifndef ERROR_CODES_H
#define ERROR_CODES_H

/* Đăng nhập / Tài khoản */
#define ERR_AUTH_NETWORK        "E1001"
#define ERR_AUTH_INVALID_INPUT  "E1002"
#define ERR_AUTH_SERVER_REJECT  "E1003"
#define ERR_AUTH_NO_SESSION     "E1004"
#define ERR_AUTH_REFRESH_FAIL   "E1005"

/* Database */
#define ERR_DB_OPEN_FAIL        "E2001"
#define ERR_DB_CREATE_TABLE     "E2002"
#define ERR_DB_INSERT_FAIL      "E2003"
#define ERR_DB_EXPORT_FAIL      "E2004"

/* Đồng bộ / Mạng */
#define ERR_SYNC_NOT_LOGGED_IN  "E3001"
#define ERR_SYNC_CONNECT_FAIL   "E3002"
#define ERR_SYNC_SERVER_ERROR   "E3003"
#define ERR_SYNC_PAYLOAD_TOO_BIG "E3004"

/* Cài đặt */
#define ERR_SETTINGS_SAVE_FAIL  "E4001"

/* Giao diện */
#define ERR_UI_TRAY_INIT_FAIL   "E5001"

/* Remote View */
#define ERR_REMOTEVIEW_SOCKET_FAIL "E6001"
#define ERR_REMOTEVIEW_BIND_FAIL   "E6002"

#endif
