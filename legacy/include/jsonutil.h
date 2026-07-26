//
// jsonutil.h
// Tiện ích dùng chung để escape chuỗi khi ghi JSON
// (dùng cho cả network.c khi gửi lên Supabase và
//  database.c khi xuất file backup).
//

#ifndef JSONUTIL_H
#define JSONUTIL_H

#ifdef __cplusplus
extern "C" {
#endif


#include <stddef.h>

/*
 * Escape một chuỗi UTF-8 để có thể đặt an toàn
 * bên trong dấu ngoặc kép của JSON.
 * output luôn được null-terminate.
 */
void json_escape(
    const char* input,
    char* output,
    size_t output_size
);


#ifdef __cplusplus
}
#endif

#endif
