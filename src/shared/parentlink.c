//
// parentlink.c
//

#include "parentlink.h"
#include "restclient.h"
#include "jsonutil.h"
#include "auth.h"

#include <stdio.h>
#include <string.h>

/*
 * Bỏ dấu ngoặc kép bao quanh 1 chuỗi JSON scalar, vd
 * "\"abc-def\"" -> "abc-def". Dùng cho response của RPC trả
 * về 1 giá trị scalar đơn (không phải object/array) - PostgREST
 * trả thẳng giá trị JSON-encode, có dấu ngoặc kép nếu là string.
 */
static void strip_json_quotes(
    const char* in,
    char* out, int out_size)
{
    size_t len = strlen(in);

    if (len >= 2 && in[0] == '"' && in[len - 1] == '"')
    {
        len -= 2;

        if ((int)len >= out_size)
            len = out_size - 1;

        memcpy(out, in + 1, len);
        out[len] = '\0';
    }
    else
    {
        snprintf(out, out_size, "%s", in);
    }
}

int parentlink_invite_child(
    const char* child_email,
    char* err_out, int err_out_size)
{
    if (!auth_is_logged_in())
    {
        snprintf(err_out, err_out_size, "Ban can dang nhap truoc.");
        return 0;
    }

    if (!child_email || child_email[0] == '\0')
    {
        snprintf(err_out, err_out_size, "Vui long nhap email cua con.");
        return 0;
    }

    /* ---- Bước 1: tra email -> user_id qua RPC ---- */

    char email_esc[400] = {0};
    json_escape(child_email, email_esc, sizeof(email_esc));

    char body[500];
    snprintf(body, sizeof(body), "{\"target_email\":\"%s\"}", email_esc);

    char response[2048] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "POST", L"/rest/v1/rpc/find_user_id_by_email",
            body, NULL,
            response, sizeof(response), &status
        )
    )
    {
        snprintf(err_out, err_out_size, "Khong the ket noi toi Supabase.");
        return 0;
    }

    if (status < 200 || status >= 300)
    {
        snprintf(err_out, err_out_size, "Loi tu server (HTTP %lu): %s", status, response);
        return 0;
    }

    if (strncmp(response, "null", 4) == 0)
    {
        snprintf(
            err_out, err_out_size,
            "Khong tim thay tai khoan nao voi email \"%s\". "
            "Con can dang ky/dang nhap JustInTime bang chinh email nay truoc.",
            child_email
        );
        return 0;
    }

    char child_user_id[MAX_LINK_UUID] = {0};
    strip_json_quotes(response, child_user_id, sizeof(child_user_id));

    if (child_user_id[0] == '\0')
    {
        snprintf(err_out, err_out_size, "Phan hoi khong hop le tu server: %s", response);
        return 0;
    }

    /* ---- Bước 2: tạo lời mời (status mặc định 'pending') ---- */

    char insert_body[128];
    snprintf(insert_body, sizeof(insert_body), "{\"child_user_id\":\"%s\"}", child_user_id);

    char insert_resp[2048] = {0};
    DWORD insert_status = 0;

    if (
        !restclient_call(
            "POST", L"/rest/v1/parent_links",
            insert_body, L"Prefer: return=minimal\r\n",
            insert_resp, sizeof(insert_resp), &insert_status
        )
    )
    {
        snprintf(err_out, err_out_size, "Khong the ket noi toi Supabase.");
        return 0;
    }

    if (insert_status >= 200 && insert_status < 300)
        return 1;

    if (insert_status == 409)
    {
        snprintf(
            err_out, err_out_size,
            "Da co lien ket (dang cho hoac da duoc chap nhan) voi tai khoan nay roi."
        );
        return 0;
    }

    snprintf(err_out, err_out_size, "Loi tu server (HTTP %lu): %s", insert_status, insert_resp);
    return 0;
}

/*
 * Dùng chung cho cả list_as_parent và list_as_child - chỉ khác
 * tên RPC và tên field (child_user_id/child_email vs
 * parent_user_id/parent_email).
 */
static int list_links(
    const wchar_t* rpc_path,
    const char* uuid_field,
    const char* email_field,
    ParentLink* out, int max_out)
{
    if (!auth_is_logged_in())
        return 0;

    char response[16384] = {0};
    DWORD status = 0;

    if (!restclient_call("POST", rpc_path, "{}", NULL, response, sizeof(response), &status))
        return 0;

    if (status < 200 || status >= 300)
        return 0;

    int count = 0;
    const char* cursor = response;
    char obj[2048];

    while (count < max_out && json_array_next(&cursor, obj, sizeof(obj)))
    {
        ParentLink* link = &out[count];
        memset(link, 0, sizeof(*link));

        long id_val = 0;
        json_extract_long(obj, "id", &id_val, NULL);
        link->id = id_val;

        json_extract_string(obj, uuid_field, link->other_user_id, sizeof(link->other_user_id));
        json_extract_string(obj, email_field, link->other_email, sizeof(link->other_email));
        json_extract_string(obj, "status", link->status, sizeof(link->status));

        count++;
    }

    return count;
}

int parentlink_list_as_parent(ParentLink* out, int max_out)
{
    return list_links(
        L"/rest/v1/rpc/parent_links_for_parent",
        "child_user_id", "child_email",
        out, max_out
    );
}

int parentlink_list_as_child(ParentLink* out, int max_out)
{
    return list_links(
        L"/rest/v1/rpc/parent_links_for_child",
        "parent_user_id", "parent_email",
        out, max_out
    );
}

static int update_status(
    long link_id,
    const char* new_status,
    char* err_out, int err_out_size)
{
    if (!auth_is_logged_in())
    {
        snprintf(err_out, err_out_size, "Ban can dang nhap truoc.");
        return 0;
    }

    wchar_t path[128];
    swprintf(path, 128, L"/rest/v1/parent_links?id=eq.%ld", link_id);

    char body[64];
    snprintf(body, sizeof(body), "{\"status\":\"%s\"}", new_status);

    char response[2048] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "PATCH", path, body,
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

int parentlink_approve(long link_id, char* err_out, int err_out_size)
{
    return update_status(link_id, "approved", err_out, err_out_size);
}

int parentlink_revoke(long link_id, char* err_out, int err_out_size)
{
    return update_status(link_id, "revoked", err_out, err_out_size);
}
