//
// applimits.c
//

#include "applimits.h"
#include "restclient.h"
#include "jsonutil.h"
#include "auth.h"

#include <windows.h>

#include <stdio.h>
#include <string.h>

static int parse_limits_response(
    const char* response,
    AppLimit* out, int max_out)
{
    int count = 0;
    const char* cursor = response;
    char obj[2048];

    while (count < max_out && json_array_next(&cursor, obj, sizeof(obj)))
    {
        AppLimit* limit = &out[count];
        memset(limit, 0, sizeof(*limit));

        long id_val = 0;
        json_extract_long(obj, "id", &id_val, NULL);
        limit->id = id_val;

        json_extract_string(obj, "process_name", limit->process_name, sizeof(limit->process_name));

        long daily_val = 0;
        int is_null = 0;

        if (json_extract_long(obj, "daily_limit_sec", &daily_val, &is_null) && !is_null)
            limit->daily_limit_sec = (int)daily_val;
        else
            limit->daily_limit_sec = -1;

        int blocked_val = 0;
        json_extract_bool(obj, "blocked", &blocked_val);
        limit->blocked = blocked_val;

        count++;
    }

    return count;
}

int applimits_list_for_child(
    const char* child_user_id,
    AppLimit* out, int max_out)
{
    if (!auth_is_logged_in() || !child_user_id || child_user_id[0] == '\0')
        return 0;

    wchar_t path[256];
    wchar_t child_id_w[64];

    MultiByteToWideChar(
        CP_UTF8, 0,
        child_user_id, -1,
        child_id_w, 64
    );

    swprintf(
        path, 256,
        L"/rest/v1/app_limits?select=*&child_user_id=eq.%ls&order=process_name.asc",
        child_id_w
    );

    char response[16384] = {0};
    DWORD status = 0;

    if (!restclient_call("GET", path, NULL, NULL, response, sizeof(response), &status))
        return 0;

    if (status < 200 || status >= 300)
        return 0;

    return parse_limits_response(response, out, max_out);
}

int applimits_set(
    const char* child_user_id,
    const char* process_name,
    int daily_limit_sec,
    int blocked,
    char* err_out, int err_out_size)
{
    if (!auth_is_logged_in())
    {
        snprintf(err_out, err_out_size, "Ban can dang nhap truoc.");
        return 0;
    }

    if (!child_user_id || !process_name || process_name[0] == '\0')
    {
        snprintf(err_out, err_out_size, "Thieu thong tin process/child.");
        return 0;
    }

    char proc_esc[600] = {0};
    json_escape(process_name, proc_esc, sizeof(proc_esc));

    char body[900];

    if (daily_limit_sec >= 0)
    {
        snprintf(
            body, sizeof(body),
            "{\"child_user_id\":\"%s\",\"process_name\":\"%s\",\"daily_limit_sec\":%d,\"blocked\":%s}",
            child_user_id, proc_esc, daily_limit_sec, blocked ? "true" : "false"
        );
    }
    else
    {
        snprintf(
            body, sizeof(body),
            "{\"child_user_id\":\"%s\",\"process_name\":\"%s\",\"daily_limit_sec\":null,\"blocked\":%s}",
            child_user_id, proc_esc, blocked ? "true" : "false"
        );
    }

    char response[2048] = {0};
    DWORD status = 0;

    int ok = restclient_call(
        "POST",
        L"/rest/v1/app_limits?on_conflict=child_user_id,process_name",
        body,
        L"Prefer: resolution=merge-duplicates,return=minimal\r\n",
        response, sizeof(response), &status
    );

    if (!ok)
    {
        snprintf(err_out, err_out_size, "Khong the ket noi toi Supabase.");
        return 0;
    }

    if (status >= 200 && status < 300)
        return 1;

    snprintf(err_out, err_out_size, "Loi tu server (HTTP %lu): %s", status, response);
    return 0;
}

int applimits_delete(
    long limit_id,
    char* err_out, int err_out_size)
{
    if (!auth_is_logged_in())
    {
        snprintf(err_out, err_out_size, "Ban can dang nhap truoc.");
        return 0;
    }

    wchar_t path[128];
    swprintf(path, 128, L"/rest/v1/app_limits?id=eq.%ld", limit_id);

    char response[2048] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "DELETE", path, NULL,
            L"Prefer: return=minimal\r\n",
            response, sizeof(response), &status
        )
    )
    {
        snprintf(err_out, err_out_size, "Khong the ket noi toi Supabase.");
        return 0;
    }

    if (status >= 200 && status < 300)
        return 1;

    snprintf(err_out, err_out_size, "Loi tu server (HTTP %lu): %s", status, response);
    return 0;
}

int applimits_get_my_limits(AppLimit* out, int max_out)
{
    if (!auth_is_logged_in())
        return 0;

    AuthSession session;
    auth_get_session(&session);

    return applimits_list_for_child(session.user_id, out, max_out);
}
