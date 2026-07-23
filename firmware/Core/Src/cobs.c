/**
  ******************************************************************************
  * @file    cobs.c
  * @brief   COBS encode/decode. See cobs.h.
  ******************************************************************************
  */
#include "cobs.h"

size_t cobs_encode(const uint8_t *src, size_t srclen, uint8_t *dst)
{
    size_t   read_idx  = 0;
    size_t   write_idx = 1;   /* leave room for the first code byte */
    size_t   code_idx  = 0;
    uint8_t  code       = 1;

    while (read_idx < srclen) {
        if (src[read_idx] == 0) {
            dst[code_idx] = code;
            code = 1;
            code_idx = write_idx++;
            read_idx++;
        } else {
            dst[write_idx++] = src[read_idx++];
            code++;
            if (code == 0xFF) {           /* max run reached, emit a code */
                dst[code_idx] = code;
                code = 1;
                code_idx = write_idx++;
            }
        }
    }
    dst[code_idx] = code;
    return write_idx;
}

size_t cobs_decode(const uint8_t *src, size_t srclen, uint8_t *dst)
{
    size_t read_idx  = 0;
    size_t write_idx = 0;

    while (read_idx < srclen) {
        uint8_t code = src[read_idx];
        if (code == 0 || read_idx + code > srclen + 1) {
            return 0;                     /* malformed */
        }
        read_idx++;
        for (uint8_t i = 1; i < code; i++) {
            dst[write_idx++] = src[read_idx++];
        }
        if (code != 0xFF && read_idx < srclen) {
            dst[write_idx++] = 0;         /* the elided zero */
        }
    }
    return write_idx;
}
