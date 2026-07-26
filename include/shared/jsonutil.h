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

/*
 * Trích 1 trường string dạng "key":"value" từ 1 đoạn JSON
 * phẳng (dò tìm text đơn giản, KHÔNG phải parser JSON đầy đủ -
 * không xử lý object lồng nhau phức tạp, nhưng đủ dùng cho các
 * response của Supabase Auth/REST vốn khá phẳng).
 *
 * Trả về 1 nếu tìm thấy key đó VÀ giá trị là string, 0 nếu
 * không tìm thấy hoặc giá trị không phải string (vd null,
 * number, object).
 */
int json_extract_string(
    const char* json,
    const char* key,
    char* out,
    int out_size
);

/*
 * Trích 1 trường boolean dạng "key":true hoặc "key":false.
 * Trả về 1 nếu tìm thấy, 0 nếu không (out_value giữ nguyên
 * nếu không tìm thấy).
 */
int json_extract_bool(
    const char* json,
    const char* key,
    int* out_value
);

/*
 * Trích 1 trường số nguyên dạng "key":123 hoặc "key":null.
 * Nếu giá trị là null: trả về 1, *out_value = 0, và
 * (nếu found_null != NULL) *found_null = 1.
 * Nếu không tìm thấy key: trả về 0.
 */
int json_extract_long(
    const char* json,
    const char* key,
    long* out_value,
    int* found_null
);

/*
 * Duyệt qua từng object trong 1 JSON array phẳng dạng
 * "[{...},{...},...]", KHÔNG xử lý được array lồng trong
 * array/object con phức tạp (đủ dùng cho các response
 * "select=*" của PostgREST, vốn là mảng các object phẳng).
 *
 * Cách dùng:
 *   const char* cursor = json_array_text;
 *   char obj[2048];
 *   while (json_array_next(&cursor, obj, sizeof(obj)))
 *   {
 *       // dùng json_extract_string/... lên "obj"
 *   }
 *
 * Trả về 1 và điền obj_out (bao gồm cả 2 dấu { }) mỗi khi còn
 * phần tử để duyệt, 0 khi đã hết mảng.
 */
int json_array_next(
    const char** cursor,
    char* obj_out,
    size_t obj_out_size
);


#ifdef __cplusplus
}
#endif

#endif
