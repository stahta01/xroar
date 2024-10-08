// Wraps config.h with guards and sets up some other useful macros
// based on its contents.

#ifndef TOP_CONFIG_H
#define TOP_CONFIG_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#ifdef HAVE___BUILTIN_EXPECT
#define LIKELY(x)   __builtin_expect(!!(x), 1)
#define UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define LIKELY(x)   (x)
#define UNLIKELY(x) (x)
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_CONST
#define FUNC_ATTR_CONST __attribute__ ((const))
#else
#define FUNC_ATTR_CONST
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_FORMAT
#define FUNC_ATTR_FORMAT_V(...) __attribute__ ((format (__VA_ARGS__)))
#else
#define FUNC_ATTR_FORMAT_V(...)
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_MALLOC
#define FUNC_ATTR_MALLOC __attribute__ ((malloc))
#define FUNC_ATTR_MALLOC_V(...) __attribute__ ((malloc (__VA_ARGS__)))
#else
#define FUNC_ATTR_MALLOC
#define FUNC_ATTR_MALLOC_V(...)
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_NONNULL
#define FUNC_ATTR_NONNULL __attribute__ ((nonnull))
#define FUNC_ATTR_NONNULL_V(...) __attribute__ ((nonnull (__VA_ARGS__)))
#else
#define FUNC_ATTR_NONNULL
#define FUNC_ATTR_NONNULL_V(...)
#endif

#if __STDC_VERSION__ >= 201112L
#define FUNC_ATTR_NORETURN _Noreturn
#elif defined(HAVE_FUNC_ATTRIBUTE_NORETURN)
#define FUNC_ATTR_NORETURN __attribute__ ((noreturn))
#else
#define FUNC_ATTR_NORETURN
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_RETURNS_NONNULL
#define FUNC_ATTR_RETURNS_NONNULL __attribute__ ((returns_nonnull))
#else
#define FUNC_ATTR_RETURNS_NONNULL
#endif

#ifdef HAVE_FUNC_ATTRIBUTE_PURE
#define FUNC_ATTR_PURE __attribute__ ((pure))
#else
#define FUNC_ATTR_PURE
#endif

#ifdef HAVE_VAR_ATTRIBUTE_PACKED
#define VAR_ATTR_PACKED __attribute__ ((packed))
#else
#define VAR_ATTR_PACKED
#endif

#endif
