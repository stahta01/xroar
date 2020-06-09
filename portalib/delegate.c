/*

Delegates in C

Copyright 2014-2018 Ciaran Anscomb

This file is part of Portalib.

Portalib is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 3 of the License, or (at your
option) any later version.

See COPYING.LGPL and COPYING.GPL for redistribution conditions.

Implements the default no-op functions for defined delegate types.

*/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdlib.h>

#include "delegate.h"

DELEGATE_DEF_FUNC0(void, void, )
DELEGATE_DEF_FUNC1(void, void, _Bool, bool, )
DELEGATE_DEF_FUNC2(void, void, _Bool, bool, uint16_t, uint16, )
DELEGATE_DEF_FUNC1(void, void, int, int, )
DELEGATE_DEF_FUNC2(void, void, int, int, uint8_t *, uint8p, )
DELEGATE_DEF_FUNC2(void, void, int, int, uint16_t *, uint16p, )
DELEGATE_DEF_FUNC3(void, void, int, int, _Bool, bool, uint16_t, uint16, )
DELEGATE_DEF_FUNC3(void, void, int, int, int, int, const void *, cvoidp, )
DELEGATE_DEF_FUNC1(void, void, unsigned, unsigned, )
DELEGATE_DEF_FUNC2(void, void, unsigned, unsigned, unsigned, unsigned, )
DELEGATE_DEF_FUNC3(void, void, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, )
DELEGATE_DEF_FUNC1(void, void, uint8_t, uint8, )
DELEGATE_DEF_FUNC2(void, void, uint16_t, uint16, uint8_t, uint8, )
DELEGATE_DEF_FUNC1(void, void, float, float, )
DELEGATE_DEF_FUNC1(void *, voidp, void *, voidp, NULL)
DELEGATE_DEF_FUNC0(_Bool, bool, 0)
DELEGATE_DEF_FUNC0(unsigned, unsigned, 0)
DELEGATE_DEF_FUNC1(unsigned, unsigned, void *, voidp, 0)
DELEGATE_DEF_FUNC0(uint8_t, uint8, 0)
DELEGATE_DEF_FUNC1(uint8_t, uint8, uint16_t, uint16, 0)
DELEGATE_DEF_FUNC0(uint8_t *, uint8p, NULL)
DELEGATE_DEF_FUNC2(void, void, uint8_t *, uint8p, unsigned, unsigned, )
DELEGATE_DEF_FUNC1(int, int, _Bool, bool, 0)
DELEGATE_DEF_FUNC3(float, float, uint32_t, uint32, int, int, float *, floatp, 0.0)
