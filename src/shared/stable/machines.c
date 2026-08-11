//
// machines.c
//
// Chế độ STABLE (C core). Xem src/shared/experimental/machines.cpp cho
// bản C++ tương đương (cùng interface machines.h).
//

#include "machines.h"
#include "restclient.h"
#include "jsonutil.h"
#include "auth.h"
#include "device.h"

#include <stdio.h>
#include <string.h>

int machines_push_heartbeat(void)
{
    if (!auth_is_logged_in())
        return 0;

    char device_id[32] = {0};
    char label[128] = {0};

    get_device_id(device_id, sizeof(device_id));
    device_get_label(label, sizeof(label));

    char device_id_esc[64] = {0};
    char label_esc[256] = {0};
    json_escape(device_id, device_id_esc, sizeof(device_id_esc));
    json_escape(label, label_esc, sizeof(label_esc));

    char body[512];
    snprintf(
        body, sizeof(body),
        "{\"device_id\":\"%s\",\"hostname\":\"%s\",\"cpu_percent\":0,"
        "\"ram_percent\":0,\"disk_percent\":0,\"last_seen\":\"now()\"}",
        device_id_esc, label_esc
    );

    char response[512] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "POST", L"/rest/v1/device_heartbeats?on_conflict=user_id,device_id",
            body, L"Prefer: resolution=merge-duplicates,return=minimal\r\n",
            response, sizeof(response), &status
        )
    )
        return 0;

    return (status >= 200 && status < 300) ? 1 : 0;
}

int machines_list(
    MachineHeartbeat* out,
    int max_entries)
{
    if (!out || max_entries <= 0)
        return 0;

    if (!auth_is_logged_in())
        return 0;

    char response[8192] = {0};
    DWORD status = 0;

    if (
        !restclient_call(
            "GET", L"/rest/v1/device_heartbeats?select=*&order=last_seen.desc",
            NULL, NULL,
            response, sizeof(response), &status
        )
    )
        return 0;

    if (status < 200 || status >= 300)
        return 0;

    const char* cursor = response;
    char obj[1024];
    int count = 0;

    while (count < max_entries && json_array_next(&cursor, obj, sizeof(obj)))
    {
        MachineHeartbeat* m = &out[count];
        memset(m, 0, sizeof(*m));

        json_extract_string(obj, "device_id", m->device_id, sizeof(m->device_id));
        json_extract_string(obj, "hostname", m->hostname, sizeof(m->hostname));
        json_extract_string(obj, "last_seen", m->last_seen, sizeof(m->last_seen));

        long cpu_l = 0, ram_l = 0, disk_l = 0;
        json_extract_long(obj, "cpu_percent", &cpu_l, NULL);
        json_extract_long(obj, "ram_percent", &ram_l, NULL);
        json_extract_long(obj, "disk_percent", &disk_l, NULL);
        m->cpu_percent = (double)cpu_l;
        m->ram_percent = (double)ram_l;
        m->disk_percent = (double)disk_l;

        count++;
    }

    return count;
}
