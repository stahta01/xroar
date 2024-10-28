/** \file
 *
 * \brief CRC-16 functions.
 */

#include "top-config.h"

#include <stdint.h>

#include "crc16.h"

/**
 * CRC-16, polynomial 0x8005 (reversed here).
 *
 * Adapted from SDL code which in turn is flagged as:
 *
 * Public domain CRC implementation adapted from:
 * http://home.thep.lu.se/~bjorn/crc/crc32_simple.c
 */

uint16_t crc16_byte(uint16_t crc, uint8_t value) {
	for (int i = 0; i < 8; ++i) {
		crc = (((crc ^ value) & 1) ? 0xa001 : 0) ^ (crc >> 1);
		value >>= 1;
	}
	return crc;
}

uint16_t crc16_block(uint16_t crc, const uint8_t *block, unsigned length) {
	for (; length; length--)
		crc = crc16_byte(crc, *(block++));
	return crc;
}

/**
 * CRC-16-CCITT with bytes processed high bit first ("big-endian"), as used in
 * the WD279X FDC (polynomial 0x1021).  In the FDC, CRC is initialised to
 * 0xffff and NOT inverted before appending to the message.
 *
 * This implementation uses some clever observations about which bits of the
 * message and old CRC affect each other.
 * \author Ashley Roll, www.digitalnemesis.com
 * \author Scott Dattalo, www.dattalo.com
 */

uint16_t crc16_ccitt_byte(uint16_t crc, uint8_t value) {
	uint16_t x = (crc >> 8) ^ value;
	x ^= (x >> 4);
	return (crc << 8) ^ (x << 12) ^ (x << 5) ^ x;
}

uint16_t crc16_ccitt_block(uint16_t crc, const uint8_t *block, unsigned length) {
	for (; length; length--)
		crc = crc16_ccitt_byte(crc, *(block++));
	return crc;
}
