/* This work is marked with CC0 1.0. To view a copy of this license, visit
 * https://creativecommons.org/publicdomain/zero/1.0/
 */

#include <stdlib.h>

/* Chaining strcpy (non-Schlemiel).  Copies from src to dst..dend inclusive,
 * potentially truncating, always null-terminating.  Returns pointer to new
 * terminator or NULL if there wasn't enough space for all of src.
 */

char *pl_estrcpy(char *dst, char *dend, const char *src) {
	if (!dst)
		return NULL;
	for (; dst < dend && (*dst = *src); ++dst, ++src);
	*dst = 0;
	return *src ? NULL : dst;
}
