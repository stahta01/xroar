/** \file
 *
 * CRC-32 implementation.  If zlib is available, this just wraps its crc32()
 * function, otherwise a table-based implementation is built.
 */

#ifndef XROAR_CRC32_H_
#define XROAR_CRC32_H_

#include "top-config.h"

#include <stdint.h>

#define CRC32_RESET (0)

uint32_t crc32_block(uint32_t crc, const uint8_t *block, unsigned length);

#endif
