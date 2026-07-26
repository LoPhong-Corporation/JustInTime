//
// Created by LoPhongCorporation on 7/2/2026.
//

#ifndef NETWORK_H
#define NETWORK_H

#ifdef __cplusplus
extern "C" {
#endif


#include "sync.h"

/*
 * Gửi một activity lên server.
 * Trả về:
 *  1 = thành công
 *  0 = thất bại
 */
int network_send_record(
    const SyncRecord* record
);


#ifdef __cplusplus
}
#endif

#endif
