/* types.h - CharisOS common type aliases */
#pragma once

/* Freestanding types */
typedef unsigned char       u8;
typedef unsigned short      u16;
typedef unsigned int        u32;
typedef unsigned long long  u64;

typedef signed char         s8;
typedef signed short        s16;
typedef signed int          s32;
typedef signed long long    s64;

typedef unsigned long       usize;
typedef signed long         isize;

/* Pointer-safe integer types */
typedef unsigned long       uintptr_t;
typedef signed long         intptr_t;

/* Fixed-width aliases commonly used in headers */
#if defined(__clang__) || defined(__GNUC__)
#include <stdint.h>
#else
typedef u8                  uint8_t;
typedef u16                 uint16_t;
typedef u32                 uint32_t;
typedef u64                 uint64_t;
typedef s8                  int8_t;
typedef s16                 int16_t;
typedef s32                 int32_t;
typedef s64                 int64_t;
#endif

typedef u8 bool;
#define true 1
#define false 0

/* Varargs for printf */
typedef __builtin_va_list va_list;
#define va_start(ap, last) __builtin_va_start(ap, last)
#define va_end(ap) __builtin_va_end(ap)
#define va_arg(ap, type) __builtin_va_arg(ap, type)

/* Packed attribute shortcut */
#define PACKED __attribute__((packed))

/* NULL pointer */
#ifndef NULL
#define NULL ((void*)0)
#endif

/* Alignment macro */
#define ALIGN_UP(x, a)   (((x) + ((a) - 1)) & ~((a) - 1))
#define ALIGN_DOWN(x, a) ((x) & ~((a) - 1))

