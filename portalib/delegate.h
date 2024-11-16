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

#ifndef PORTALIB_DELEGATE_H_
#define PORTALIB_DELEGATE_H_

#include <stdint.h>

#define DELEGATE_DEFINED(d) ((d).func != NULL)

/* Underlying struct def for delegates. */

#define DELEGATE_S0(T) struct { T (*func)(void *); void *sptr; }
#define DELEGATE_S1(T,T0) struct { T (*func)(void *, T0); void *sptr; }
#define DELEGATE_S2(T,T0,T1) struct { T (*func)(void *, T0, T1); void *sptr; }
#define DELEGATE_S3(T,T0,T1,T2) struct { T (*func)(void *, T0, T1, T2); void *sptr; }
#define DELEGATE_S4(T,T0,T1,T2,T3) struct { T (*func)(void *, T0, T1, T2, T3); void *sptr; }
#define DELEGATE_S5(T,T0,T1,T2,T3,T4) struct { T (*func)(void *, T0, T1, T2, T3, T4); void *sptr; }
#define DELEGATE_S6(T,T0,T1,T2,T3,T4,T5) struct { T (*func)(void *, T0, T1, T2, T3, T4, T5); void *sptr; }

/* Type name for delegates. */

#define DELEGATE_T0(N) delegate_##N
#define DELEGATE_T1(N,N0) delegate_##N##_##N0
#define DELEGATE_T2(N,N0,N1) delegate_##N##_##N0##_##N1
#define DELEGATE_T3(N,N0,N1,N2) delegate_##N##_##N0##_##N1##_##N2
#define DELEGATE_T4(N,N0,N1,N2,N3) delegate_##N##_##N0##_##N1##_##N2##_##N3
#define DELEGATE_T5(N,N0,N1,N2,N3,N4) delegate_##N##_##N0##_##N1##_##N2##_##N3##_##N4
#define DELEGATE_T6(N,N0,N1,N2,N3,N4,N5) delegate_##N##_##N0##_##N1##_##N2##_##N3##_##N4##_##N5

/* Define a set of delegate types. */

typedef DELEGATE_S0(void) DELEGATE_T0(void);
typedef DELEGATE_S1(void, _Bool) DELEGATE_T1(void, bool);
typedef DELEGATE_S2(void, _Bool, uint16_t) DELEGATE_T2(void, bool, uint16);
typedef DELEGATE_S1(void, int) DELEGATE_T1(void, int);
typedef DELEGATE_S2(void, int, _Bool) DELEGATE_T2(void, int, bool);
typedef DELEGATE_S2(void, int, int) DELEGATE_T2(void, int, int);
typedef DELEGATE_S4(void, int, int, int, int) DELEGATE_T4(void, int, int, int, int);
typedef DELEGATE_S3(void, int, int, double) DELEGATE_T3(void, int, int, double);
typedef DELEGATE_S3(void, int, _Bool, uint16_t) DELEGATE_T3(void, int, bool, uint16);
typedef DELEGATE_S3(void, int, int, const void *) DELEGATE_T3(void, int, int, cvoidp);
typedef DELEGATE_S2(void, int, uint8_t *) DELEGATE_T2(void, int, uint8p);
typedef DELEGATE_S2(void, int, uint16_t *) DELEGATE_T2(void, int, uint16p);
typedef DELEGATE_S1(void, unsigned) DELEGATE_T1(void, unsigned);
typedef DELEGATE_S2(void, unsigned, int) DELEGATE_T2(void, unsigned, int);
typedef DELEGATE_S2(void, unsigned, unsigned) DELEGATE_T2(void, unsigned, unsigned);
typedef DELEGATE_S3(void, unsigned, unsigned, unsigned) DELEGATE_T3(void, unsigned, unsigned, unsigned);
typedef DELEGATE_S3(void, unsigned, unsigned, uint8_t const *)
	DELEGATE_T3(void, unsigned, unsigned, uint8cp);
typedef DELEGATE_S3(void, unsigned, float, float) DELEGATE_T3(void, unsigned, float, float);
typedef DELEGATE_S1(void, uint8_t) DELEGATE_T1(void, uint8);
typedef DELEGATE_S4(void, uint8_t, float, float, float) DELEGATE_T4(void, uint8, float, float, float);
typedef DELEGATE_S2(void, uint8_t *, unsigned) DELEGATE_T2(void, uint8p, unsigned);
typedef DELEGATE_S3(void, uint8_t const *, unsigned, unsigned)
	DELEGATE_T3(void, uint8cp, unsigned, unsigned);
typedef DELEGATE_S3(void, uint16_t, int, uint16_t *) DELEGATE_T3(void, uint16, int, uint16p);
typedef DELEGATE_S2(void, uint16_t, uint8_t) DELEGATE_T2(void, uint16, uint8);
typedef DELEGATE_S1(void, float) DELEGATE_T1(void, float);
typedef DELEGATE_S2(void, float, float) DELEGATE_T2(void, float, float);
typedef DELEGATE_S1(void *, void *) DELEGATE_T1(voidp, voidp);
typedef DELEGATE_S0(_Bool) DELEGATE_T0(bool);
typedef DELEGATE_S0(unsigned) DELEGATE_T0(unsigned);
typedef DELEGATE_S1(unsigned, int) DELEGATE_T1(unsigned, int);
typedef DELEGATE_S1(unsigned, void *) DELEGATE_T1(unsigned, voidp);
typedef DELEGATE_S0(uint8_t) DELEGATE_T0(uint8);
typedef DELEGATE_S2(uint8_t, uint8_t, _Bool) DELEGATE_T2(uint8, uint8, bool);
typedef DELEGATE_S1(uint8_t, uint16_t) DELEGATE_T1(uint8, uint16);
typedef DELEGATE_S1(uint8_t, uint32_t) DELEGATE_T1(uint8, uint32);
typedef DELEGATE_S0(uint8_t *) DELEGATE_T0(uint8p);
typedef DELEGATE_S3(uint8_t *, unsigned, unsigned, unsigned) DELEGATE_T3(uint8p, unsigned, unsigned, unsigned);
typedef DELEGATE_S1(uint16_t, uint32_t) DELEGATE_T1(uint16, uint32);
typedef DELEGATE_S1(int, _Bool) DELEGATE_T1(int, bool);
typedef DELEGATE_S3(float, uint32_t, int, float *) DELEGATE_T3(float, uint32, int, floatp);

/* Convenience function for declaring anonymous structs. */

#define DELEGATE_INIT(f,s) {f,s}

#define DELEGATE_AS0(N,f,s) (DELEGATE_T0(N))DELEGATE_INIT(f,s)
#define DELEGATE_AS1(N,N0,f,s) (DELEGATE_T1(N,N0))DELEGATE_INIT(f,s)
#define DELEGATE_AS2(N,N0,N1,f,s) (DELEGATE_T2(N,N0,N1))DELEGATE_INIT(f,s)
#define DELEGATE_AS3(N,N0,N1,N2,f,s) (DELEGATE_T3(N,N0,N1,N2))DELEGATE_INIT(f,s)
#define DELEGATE_AS4(N,N0,N1,N2,N3,f,s) (DELEGATE_T4(N,N0,N1,N2,N3))DELEGATE_INIT(f,s)
#define DELEGATE_AS5(N,N0,N1,N2,N3,N4,f,s) (DELEGATE_T5(N,N0,N1,N2,N3,N4))DELEGATE_INIT(f,s)
#define DELEGATE_AS6(N,N0,N1,N2,N3,N4,N5,f,s) (DELEGATE_T6(N,N0,N1,N2,N3,N4,N5))DELEGATE_INIT(f,s)

/* Delegate default function names. */

#define DELEGATE_DEFAULT_F0(N) delegate_##N##_default
#define DELEGATE_DEFAULT_F1(N,N0) delegate_##N##_default_##N0
#define DELEGATE_DEFAULT_F2(N,N0,N1) delegate_##N##_default_##N0##_##N1
#define DELEGATE_DEFAULT_F3(N,N0,N1,N2) delegate_##N##_default_##N0##_##N1##_##N2
#define DELEGATE_DEFAULT_F4(N,N0,N1,N2,N3) delegate_##N##_default_##N0##_##N1##_##N2##_##N3
#define DELEGATE_DEFAULT_F5(N,N0,N1,N2,N3,N4) delegate_##N##_default_##N0##_##N1##_##N2##_##N3##_##N4
#define DELEGATE_DEFAULT_F6(N,N0,N1,N2,N3,N4,N5) delegate_##N##_default_##N0##_##N1##_##N2##_##N3##_##N4##_##N5

/* Default no-op functions for defined delegate types. */

#define DELEGATE_DEF_PROTO0(T,N) T DELEGATE_DEFAULT_F0(N)(void *)
#define DELEGATE_DEF_PROTO1(T,N,T0,N0) T DELEGATE_DEFAULT_F1(N,N0)(void *, T0)
#define DELEGATE_DEF_PROTO2(T,N,T0,N0,T1,N1) T DELEGATE_DEFAULT_F2(N,N0,N1)(void *, T0, T1)
#define DELEGATE_DEF_PROTO3(T,N,T0,N0,T1,N1,T2,N2) T DELEGATE_DEFAULT_F3(N,N0,N1,N2)(void *, T0, T1, T2)
#define DELEGATE_DEF_PROTO4(T,N,T0,N0,T1,N1,T2,N2,T3,N3) T DELEGATE_DEFAULT_F4(N,N0,N1,N2,N3)(void *, T0, T1, T2, T3)
#define DELEGATE_DEF_PROTO5(T,N,T0,N0,T1,N1,T2,N2,T3,N3,T4,N4) T DELEGATE_DEFAULT_F5(N,N0,N1,N2,N3,N4)(void *, T0, T1, T2, T3, T4)
#define DELEGATE_DEF_PROTO6(T,N,T0,N0,T1,N1,T2,N2,T3,N3,T4,N4,T5,N5) T DELEGATE_DEFAULT_F6(N,N0,N1,N2,N3,N4,N5)(void *, T0, T1, T2, T3, T4, T5)

#define DELEGATE_DEF_FUNC0(T,N,R) T DELEGATE_DEFAULT_F0(N)(void *sptr) { (void)sptr; return R; }
#define DELEGATE_DEF_FUNC1(T,N,T0,N0,R) T DELEGATE_DEFAULT_F1(N,N0)(void *sptr, T0 v0) { (void)sptr; (void)v0; return R; }
#define DELEGATE_DEF_FUNC2(T,N,T0,N0,T1,N1,R) T DELEGATE_DEFAULT_F2(N,N0,N1)(void *sptr, T0 v0, T1 v1) { (void)sptr; (void)v0; (void)v1; return R; }
#define DELEGATE_DEF_FUNC3(T,N,T0,N0,T1,N1,T2,N2,R) T DELEGATE_DEFAULT_F3(N,N0,N1,N2)(void *sptr, T0 v0, T1 v1, T2 v2) { (void)sptr; (void)v0; (void)v1; (void)v2; return R; }
#define DELEGATE_DEF_FUNC4(T,N,T0,N0,T1,N1,T2,N2,T3,N3,R) T DELEGATE_DEFAULT_F4(N,N0,N1,N2,N3)(void *sptr, T0 v0, T1 v1, T2 v2, T3 v3) { (void)sptr; (void)v0; (void)v1; (void)v2; (void)v3; return R; }
#define DELEGATE_DEF_FUNC5(T,N,T0,N0,T1,N1,T2,N2,T3,N3,T4,N4,R) T DELEGATE_DEFAULT_F5(N,N0,N1,N2,N3,N4)(void *sptr, T0 v0, T1 v1, T2 v2, T3 v3, T4 v4) { (void)sptr; (void)v0; (void)v1; (void)v2; (void)v3; (void)v4; return R; }
#define DELEGATE_DEF_FUNC6(T,N,T0,N0,T1,N1,T2,N2,T3,N3,T4,N4,T5,N5,R) T DELEGATE_DEFAULT_F6(N,N0,N1,N2,N3,N4,N5)(void *sptr, T0 v0, T1 v1, T2 v2, T3 v3, T4 v4, T5 v5) { (void)sptr; (void)v0; (void)v1; (void)v2; (void)v3; (void)v4; (void)v5; return R; }

DELEGATE_DEF_PROTO0(void, void);
DELEGATE_DEF_PROTO1(void, void, _Bool, bool);
DELEGATE_DEF_PROTO2(void, void, _Bool, bool, uint16_t, uint16);
DELEGATE_DEF_PROTO1(void, void, int, int);
DELEGATE_DEF_PROTO2(void, void, int, int, _Bool, bool);
DELEGATE_DEF_PROTO2(void, void, int, int, int, int);
DELEGATE_DEF_PROTO4(void, void, int, int, int, int, int, int, int, int);
DELEGATE_DEF_PROTO3(void, void, int, int, int, int, double, double);
DELEGATE_DEF_PROTO2(void, void, int, int, uint8_t *, uint8p);
DELEGATE_DEF_PROTO2(void, void, int, int, uint16_t *, uint16p);
DELEGATE_DEF_PROTO3(void, void, int, int, _Bool, bool, uint16_t, uint16);
DELEGATE_DEF_PROTO3(void, void, int, int, int, int, const void *, cvoidp);
DELEGATE_DEF_PROTO1(void, void, unsigned, unsigned);
DELEGATE_DEF_PROTO2(void, void, unsigned, unsigned, int, int);
DELEGATE_DEF_PROTO2(void, void, unsigned, unsigned, unsigned, unsigned);
DELEGATE_DEF_PROTO3(void, void, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
DELEGATE_DEF_PROTO3(void, void, unsigned, unsigned, unsigned, unsigned, uint8_t const *, uint8cp);
DELEGATE_DEF_PROTO3(void, void, unsigned, unsigned, float, float, float, float);
DELEGATE_DEF_PROTO1(void, void, uint8_t, uint8);
DELEGATE_DEF_PROTO4(void, void, uint8_t, uint8, float, float, float, float, float, float);
DELEGATE_DEF_PROTO2(void, void, uint8_t *, uint8p, unsigned, unsigned);
DELEGATE_DEF_PROTO3(void, void, uint8_t const *, uint8cp, unsigned, unsigned, unsigned, unsigned);
DELEGATE_DEF_PROTO3(void, void, uint16_t, uint16, int, int, uint16_t *, uint16p);
DELEGATE_DEF_PROTO2(void, void, uint16_t, uint16, uint8_t, uint8);
DELEGATE_DEF_PROTO1(void, void, float, float);
DELEGATE_DEF_PROTO2(void, void, float, float, float, float);
DELEGATE_DEF_PROTO1(void *, voidp, void *, voidp);
DELEGATE_DEF_PROTO0(_Bool, bool);
DELEGATE_DEF_PROTO0(unsigned, unsigned);
DELEGATE_DEF_PROTO1(unsigned, unsigned, int, int);
DELEGATE_DEF_PROTO1(unsigned, unsigned, void *, voidp);
DELEGATE_DEF_PROTO0(uint8_t, uint8);
DELEGATE_DEF_PROTO2(uint8_t, uint8, uint8_t, uint8, _Bool, bool);
DELEGATE_DEF_PROTO1(uint8_t, uint8, uint16_t, uint16);
DELEGATE_DEF_PROTO1(uint8_t, uint8, uint32_t, uint32);
DELEGATE_DEF_PROTO1(uint16_t, uint16, uint32_t, uint32);
DELEGATE_DEF_PROTO0(uint8_t *, uint8p);
DELEGATE_DEF_PROTO3(uint8_t *, uint8p, unsigned, unsigned, unsigned, unsigned, unsigned, unsigned);
DELEGATE_DEF_PROTO1(int, int, _Bool, bool);
DELEGATE_DEF_PROTO3(float, float, uint32_t, uint32, int, int, float *, floatp);

#define DELEGATE_DEFAULT0(N) DELEGATE_AS0(N, DELEGATE_DEFAULT_F0(N), NULL)
#define DELEGATE_DEFAULT1(N,N0) DELEGATE_AS1(N, N0, DELEGATE_DEFAULT_F1(N, N0), NULL)
#define DELEGATE_DEFAULT2(N,N0,N1) DELEGATE_AS2(N, N0, N1, DELEGATE_DEFAULT_F2(N, N0, N1), NULL)
#define DELEGATE_DEFAULT3(N,N0,N1,N2) DELEGATE_AS3(N, N0, N1, N2, DELEGATE_DEFAULT_F3(N, N0, N1, N2), NULL)
#define DELEGATE_DEFAULT4(N,N0,N1,N2,N3) DELEGATE_AS4(N, N0, N1, N2, N3, DELEGATE_DEFAULT_F4(N, N0, N1, N2, N3), NULL)
#define DELEGATE_DEFAULT5(N,N0,N1,N2,N3,N4) DELEGATE_AS5(N, N0, N1, N2, N3, N4, DELEGATE_DEFAULT_F5(N, N0, N1, N2, N3, N4), NULL)
#define DELEGATE_DEFAULT6(N,N0,N1,N2,N3,N4,N5) DELEGATE_AS6(N, N0, N1, N2, N3, N4, N5, DELEGATE_DEFAULT_F6(N, N0, N1, N2, N3, N4, N5), NULL)

/* Calling interface. */

#define DELEGATE_CALL0(d) ((d).func((d).sptr))
#define DELEGATE_CALLN(d,...) ((d).func((d).sptr,__VA_ARGS__))
#define DELEGATE_SAFE_CALL0(d) do { if ((d).func) { DELEGATE_CALL0((d)); } } while (0)
#define DELEGATE_SAFE_CALLN(d,...) do { if ((d).func) { ((d).func((d).sptr,__VA_ARGS__)); } } while (0)

#define DELEGATE_GET_CALL(_1,_2,_3,_4,_5,_6,_7,NAME,...) NAME

#define DELEGATE_CALL(...) DELEGATE_GET_CALL(__VA_ARGS__, DELEGATE_CALLN, DELEGATE_CALLN, DELEGATE_CALLN, DELEGATE_CALLN, DELEGATE_CALLN, DELEGATE_CALLN, DELEGATE_CALL0)(__VA_ARGS__)
#define DELEGATE_SAFE_CALL(...) DELEGATE_GET_CALL(__VA_ARGS__, DELEGATE_SAFE_CALLN, DELEGATE_SAFE_CALLN, DELEGATE_SAFE_CALLN, DELEGATE_SAFE_CALLN, DELEGATE_SAFE_CALLN, DELEGATE_SAFE_CALLN, DELEGATE_SAFE_CALL0)(__VA_ARGS__)

#endif
