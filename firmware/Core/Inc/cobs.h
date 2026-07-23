/**
  ******************************************************************************
  * @file    cobs.h
  * @brief   Consistent Overhead Byte Stuffing (COBS) framing for the WCL UART.
  *
  * COBS removes every 0x00 byte from a payload so that 0x00 can be used as an
  * unambiguous frame delimiter. Overhead is at most 1 byte per 254 bytes of
  * payload, which is negligible for our ~30-byte protobuf frames.
  *
  * Reference: Cheshire & Baker, "Consistent Overhead Byte Stuffing", 1999.
  ******************************************************************************
  */
#ifndef COBS_H
#define COBS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Encode @p srclen bytes from @p src into @p dst using COBS.
 * @note  @p dst must hold at least cobs_encode_bufsize(srclen) bytes.
 *        The caller appends the 0x00 delimiter after the encoded block.
 * @return number of bytes written to @p dst (excludes the delimiter).
 */
size_t cobs_encode(const uint8_t *src, size_t srclen, uint8_t *dst);

/**
 * @brief Decode a COBS block (delimiter already stripped) into @p dst.
 * @return number of decoded bytes, or 0 on a malformed frame.
 */
size_t cobs_decode(const uint8_t *src, size_t srclen, uint8_t *dst);

/** Worst-case encoded size for a payload of @p srclen bytes. */
#define cobs_encode_bufsize(srclen)  ((srclen) + ((srclen) / 254u) + 1u)

#ifdef __cplusplus
}
#endif

#endif /* COBS_H */
