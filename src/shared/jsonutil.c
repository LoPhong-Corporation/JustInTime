//
// jsonutil.c
//

#include "jsonutil.h"

#include <stdio.h>
#include <string.h>

void json_escape(
    const char* input,
    char* output,
    size_t output_size)
{
    if (!output || output_size == 0)
        return;

    if (!input)
    {
        output[0] = '\0';
        return;
    }

    size_t j = 0;

    for (size_t i = 0; input[i] != '\0'; i++)
    {
        unsigned char c = (unsigned char)input[i];

        /*
         * Chừa ít nhất 7 ký tự trống để có thể
         * ghi an toàn chuỗi \u00XX dài nhất.
         */
        if (j + 7 >= output_size)
            break;

        switch (c)
        {
            case '"':
                output[j++] = '\\';
                output[j++] = '"';
                break;

            case '\\':
                output[j++] = '\\';
                output[j++] = '\\';
                break;

            case '\n':
                output[j++] = '\\';
                output[j++] = 'n';
                break;

            case '\r':
                output[j++] = '\\';
                output[j++] = 'r';
                break;

            case '\t':
                output[j++] = '\\';
                output[j++] = 't';
                break;

            default:
                if (c < 0x20)
                {
                    j += (size_t)snprintf(
                        output + j,
                        output_size - j,
                        "\\u%04x",
                        c
                    );
                }
                else
                {
                    output[j++] = (char)c;
                }
        }
    }

    output[j] = '\0';
}

/*
 * Tìm vị trí bắt đầu giá trị (sau dấu ':', bỏ khoảng trắng)
 * ứng với "key" trong json. Trả về NULL nếu không tìm thấy key.
 */
static const char* find_value_pos(
    const char* json,
    const char* key)
{
    char pattern[160];

    snprintf(pattern, sizeof(pattern), "\"%s\"", key);

    const char* pos = strstr(json, pattern);

    if (!pos)
        return NULL;

    pos = strchr(pos + strlen(pattern), ':');

    if (!pos)
        return NULL;

    pos++;

    while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
        pos++;

    return pos;
}

int json_extract_string(
    const char* json,
    const char* key,
    char* out,
    int out_size)
{
    const char* pos = find_value_pos(json, key);

    if (!pos || *pos != '"')
        return 0;

    pos++;

    int i = 0;

    while (*pos && *pos != '"' && i < out_size - 1)
    {
        if (*pos == '\\' && *(pos + 1) != '\0')
        {
            pos++;
            out[i++] = *pos;
        }
        else
        {
            out[i++] = *pos;
        }

        pos++;
    }

    out[i] = '\0';

    return 1;
}

int json_extract_bool(
    const char* json,
    const char* key,
    int* out_value)
{
    const char* pos = find_value_pos(json, key);

    if (!pos)
        return 0;

    if (strncmp(pos, "true", 4) == 0)
    {
        *out_value = 1;
        return 1;
    }

    if (strncmp(pos, "false", 5) == 0)
    {
        *out_value = 0;
        return 1;
    }

    return 0;
}

int json_extract_long(
    const char* json,
    const char* key,
    long* out_value,
    int* found_null)
{
    const char* pos = find_value_pos(json, key);

    if (!pos)
        return 0;

    if (strncmp(pos, "null", 4) == 0)
    {
        *out_value = 0;

        if (found_null)
            *found_null = 1;

        return 1;
    }

    if (found_null)
        *found_null = 0;

    *out_value = strtol(pos, NULL, 10);

    return 1;
}

int json_array_next(
    const char** cursor,
    char* obj_out,
    size_t obj_out_size)
{
    if (!cursor || !*cursor)
        return 0;

    const char* p = *cursor;

    /*
     * Bỏ qua khoảng trắng, dấu '[' đầu mảng, và dấu ','
     * ngăn cách giữa 2 phần tử.
     */
    while (
        *p == ' ' || *p == '\t' || *p == '\n' || *p == '\r' ||
        *p == '[' || *p == ','
    )
        p++;

    if (*p != '{')
    {
        /*
         * Đã hết mảng (gặp ']') hoặc chuỗi rỗng/không hợp lệ.
         */
        *cursor = p;
        return 0;
    }

    /*
     * Đếm ngoặc lồng nhau (bỏ qua { } nằm bên trong chuỗi
     * string, để không đếm nhầm nếu 1 giá trị string chứa
     * ký tự '{'/'}').
     */
    int depth = 0;
    int in_string = 0;
    const char* start = p;

    while (*p)
    {
        char c = *p;

        if (in_string)
        {
            if (c == '\\' && *(p + 1) != '\0')
            {
                p += 2;
                continue;
            }

            if (c == '"')
                in_string = 0;

            p++;
            continue;
        }

        if (c == '"')
        {
            in_string = 1;
            p++;
            continue;
        }

        if (c == '{')
            depth++;

        if (c == '}')
        {
            depth--;

            if (depth == 0)
            {
                p++;
                break;
            }
        }

        p++;
    }

    size_t len = (size_t)(p - start);

    if (len >= obj_out_size)
        len = obj_out_size - 1;

    memcpy(obj_out, start, len);
    obj_out[len] = '\0';

    *cursor = p;

    return 1;
}
