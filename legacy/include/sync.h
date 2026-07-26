//
// Created by LoPhongCorporation on 6/24/2026.
//

#ifndef SYNC_H
#define SYNC_H

#ifdef __cplusplus
extern "C" {
#endif


#include <wchar.h>

#define MAX_RECORDS 100

typedef struct
{
    int id;

    char device_id[128];

    wchar_t process_name[512];

    wchar_t window_title[2048];

    long duration_seconds;

    long long start_time;
    long long end_time;

} SyncRecord;

void sync_pending_records(void);


#ifdef __cplusplus
}
#endif

#endif
