//
// jsonutil.c
//

#include "../include/jsonutil.h"

#include <stdio.h>

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
