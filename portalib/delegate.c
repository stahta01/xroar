/** \file
 *
 *  \brief Delegates in C.
 *
 *  \copyright Copyright 2014-2023 Ciaran Anscomb
 *
 *  \licenseblock This file is part of Portalib.
 *
 *  Portalib is free software; you can redistribute it and/or modify it under
 *  the terms of the GNU Lesser General Public License as published by the Free
 *  Software Foundation; either version 3 of the License, or (at your option)
 *  any later version.
 *
 *  See COPYING.LGPL and COPYING.GPL for redistribution conditions.
 *
 *  \endlicenseblock
 *
 *  Implements the default no-op functions for defined delegate types.
 */

#include "top-config.h"

#include <stdlib.h>

#include "delegate.h"

DELEGATE_DEF_FUNC0(void, void, )
DELEGATE_DEF_FUNC1(void, void, bool, bool, )
DELEGATE_DEF_FUNC2(void, void, bool, bool, uint16_t, uint16, )
DELEGATE_DEF_FUNC2(void, void, bool, bool, uint32_t, uint32, )
DELEGATE_DEF_FUNC1(void, void, int, int, )
DELEGATE_DEF_FUNC2(void, void, int, int, bool, bool, )
DELEGATE_DEF_FUNC2(void, void, int, int, int, int, )
DELEGATE_DEF_FUNC4(void, void, int, int, int, int, int, int, int, int, )
DELEGATE_DEF_FUNC3(void, void, int, int, int, int, double, double, )
DELEGATE_DEF_FUNC2(void, void, int, int, uint8_t *, uint8p, )
DELEGATE_DEF_FUNC2(void, void, int, int, uint16_t *, uint16p, )
DELEGATE_DEF_FUNC2(void, void, int, int, uint32_t, uint32, )
DELEGATE_DEF_FUNC3(void, void, int, int, bool, bool, uint16_t, uint16, )
DELEGATE_DEF_FUNC3(void, void, int, int, int, int, const void *, cvoidp, )
DELEGATE_DEF_FUNC1(void, void, unsigned, unsigned, )
DELEGATE_DEF_FUNC2(void, void, unsigned, unsigned, int, int, )
DELEGATE_DEF_FUNC2(void, void, unsigned, unsigned, unsigned, unsigned, )
DELEGATE_DEF_FUNC3(void, void, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, )
DELEGATE_DEF_FUNC3(void, void, unsigned, unsigned, unsigned, unsigned, uint8_t const *, uint8cp, )
DELEGATE_DEF_FUNC2(void, void, unsigned, unsigned, uint32_t, uint32, )
DELEGATE_DEF_FUNC3(void, void, unsigned, unsigned, float, float, float, float, )
DELEGATE_DEF_FUNC1(void, void, uint8_t, uint8, )
DELEGATE_DEF_FUNC4(void, void, uint8_t, uint8, float, float, float, float, float, float, )
DELEGATE_DEF_FUNC2(void, void, uint8_t *, uint8p, unsigned, unsigned, )
DELEGATE_DEF_FUNC3(void, void, uint8_t const *, uint8cp, unsigned, unsigned, unsigned, unsigned, )
DELEGATE_DEF_FUNC3(void, void, uint16_t, uint16, int, int, uint16_t *, uint16p, )
DELEGATE_DEF_FUNC2(void, void, uint16_t, uint16, uint8_t, uint8, )
DELEGATE_DEF_FUNC1(void, void, uint32_t, uint32, )
DELEGATE_DEF_FUNC1(void, void, float, float, )
DELEGATE_DEF_FUNC2(void, void, float, float, float, float, )
DELEGATE_DEF_FUNC1(void *, voidp, void *, voidp, NULL)
DELEGATE_DEF_FUNC0(bool, bool, 0)
DELEGATE_DEF_FUNC1(bool, bool, int, int, 0)
DELEGATE_DEF_FUNC1(const char *, charcp, int, int, NULL)
DELEGATE_DEF_FUNC1(const char *, charcp, unsigned, unsigned, NULL)
DELEGATE_DEF_FUNC0(unsigned, unsigned, 0)
DELEGATE_DEF_FUNC1(unsigned, unsigned, int, int, 0)
DELEGATE_DEF_FUNC1(unsigned, unsigned, unsigned, unsigned, 0)
DELEGATE_DEF_FUNC1(unsigned, unsigned, void *, voidp, 0)
DELEGATE_DEF_FUNC0(uint8_t, uint8, 0)
DELEGATE_DEF_FUNC2(uint8_t, uint8, uint8_t, uint8, bool, bool, 0)
DELEGATE_DEF_FUNC1(uint8_t, uint8, uint16_t, uint16, 0)
DELEGATE_DEF_FUNC1(uint8_t, uint8, uint32_t, uint32, 0)
DELEGATE_DEF_FUNC1(uint16_t, uint16, uint32_t, uint32, 0)
DELEGATE_DEF_FUNC1(uint32_t, uint32, int, int, 0)
DELEGATE_DEF_FUNC1(uint32_t, uint32, unsigned, unsigned, 0)
DELEGATE_DEF_FUNC0(uint8_t *, uint8p, NULL)
DELEGATE_DEF_FUNC3(uint8_t *, uint8p, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned, NULL)
DELEGATE_DEF_FUNC1(int, int, bool, bool, 0)
DELEGATE_DEF_FUNC3(int, int, int, int, unsigned, unsigned, uint8_t *, uint8p, 0)
DELEGATE_DEF_FUNC3(int, int, int, int, unsigned, unsigned, const uint8_t *, cuint8p, 0)
DELEGATE_DEF_FUNC3(float, float, uint32_t, uint32, int, int, float *, floatp, 0.0)
