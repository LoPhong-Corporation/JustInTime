//
// parentlink.cpp
//
// Đã CHUYỂN TỪ C SANG C++ (giữ nguyên interface extern "C" trong
// parentlink.h). Logic giữ nguyên 100% - chỉ đổi cách dựng
// path/body sang std::string.
//

#include "parentlink.h"
#include "restclient.h"
#include "jsonutil.h"
#include "auth.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace {

std::string jsonEscape(const std::string& s)
{
    std::string out(s.size() * 6 + 16, '\0');
    json_escape(s.c_str(), out.data(), out.size());
    out.resize(strlen(out.c_str()));
    return out;
}

/*
 * Bỏ dấu ngoặc kép bao quanh 1 chuỗi JSON scalar, vd
 * "\"abc-def\"" -> "abc-def". Dùng cho response của RPC trả về 1
 * giá trị scalar đơn (không phải object/array) - PostgREST trả
 * thẳng giá trị JSON-encode, có dấu ngoặc kép nếu là string.
 */
std::string stripJsonQuotes(const std::string& in)
{
    if (in.size() >= 2 && in.front() == '"' && in.back() == '"')
        return in.substr(1, in.size() - 2);
    return in;
}

/*
 * Dùng chung cho cả list_as_parent và list_as_child - chỉ khác
 * tên RPC và tên field (child_user_id/child_email vs
 * parent_user_id/parent_email).
 */
int listLinks(
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

int updateStatus(
    long link_id,
    const char* new_status,
    char* err_out, int err_out_size)
{
    if (!auth_is_logged_in())
    {
        snprintf(err_out, err_out_size, "Ban can dang nhap truoc.");
        return 0;
    }

    const std::wstring path = L"/rest/v1/parent_links?id=eq." + std::to_wstring(link_id);
    const std::string body = std::string("{\"status\":\"") + new_status + "\"}";

    char response[2048] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "PATCH", path.c_str(), body.c_str(),
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

} // namespace

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

    const std::string emailEsc = jsonEscape(child_email);
    const std::string body = "{\"target_email\":\"" + emailEsc + "\"}";

    char response[2048] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "POST", L"/rest/v1/rpc/find_user_id_by_email",
            body.c_str(), NULL,
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

    const std::string childUserId = stripJsonQuotes(response);

    if (childUserId.empty())
    {
        snprintf(err_out, err_out_size, "Phan hoi khong hop le tu server: %s", response);
        return 0;
    }
    if (childUserId.size() >= static_cast<size_t>(MAX_LINK_UUID))
    {
        snprintf(err_out, err_out_size, "ID tra ve tu server qua dai, khong hop le.");
        return 0;
    }

    /* ---- Bước 2: tạo lời mời (status mặc định 'pending') ---- */

    const std::string insertBody = "{\"child_user_id\":\"" + childUserId + "\"}";

    char insertResp[2048] = {0};
    DWORD insertStatus = 0;

    if (
        !restclient_call(
            "POST", L"/rest/v1/parent_links",
            insertBody.c_str(), L"Prefer: return=minimal\r\n",
            insertResp, sizeof(insertResp), &insertStatus
        )
    )
    {
        snprintf(err_out, err_out_size, "Khong the ket noi toi Supabase.");
        return 0;
    }

    if (insertStatus >= 200 && insertStatus < 300)
        return 1;

    if (insertStatus == 409)
    {
        snprintf(
            err_out, err_out_size,
            "Da co lien ket (dang cho hoac da duoc chap nhan) voi tai khoan nay roi."
        );
        return 0;
    }

    snprintf(err_out, err_out_size, "Loi tu server (HTTP %lu): %s", insertStatus, insertResp);
    return 0;
}

int parentlink_list_as_parent(ParentLink* out, int max_out)
{
    return listLinks(
        L"/rest/v1/rpc/parent_links_for_parent",
        "child_user_id", "child_email",
        out, max_out
    );
}

int parentlink_list_as_child(ParentLink* out, int max_out)
{
    return listLinks(
        L"/rest/v1/rpc/parent_links_for_child",
        "parent_user_id", "parent_email",
        out, max_out
    );
}

int parentlink_approve(long link_id, char* err_out, int err_out_size)
{
    return updateStatus(link_id, "approved", err_out, err_out_size);
}

int parentlink_revoke(long link_id, char* err_out, int err_out_size)
{
    return updateStatus(link_id, "revoked", err_out, err_out_size);
}
