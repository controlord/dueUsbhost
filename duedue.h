// Arduino due


#ifndef DUEDUEDUE
#define DUEDUEDUE
#ifndef __SAM3X8E__
#define __SAM3X8E__
#endif
#include <Arduino.h>
#include <sam3xa/include/sam3xa.h>
#include <sys/types.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdarg.h>

#ifdef __cplusplus
#define CCFUNCTION(xxx)  extern "C" { xxx; }
#else
#define CCFUNCTION(xxx)  xxx;
#endif
#if (defined __GNUC__) || (defined __CC_ARM)
#define ALIGN16 __attribute__((aligned(2)))
#define ALIGN32 __attribute__((aligned(4)))
#define ISALIGN32(x) ((((u32)(x))&0x3)==0)
#endif

typedef unsigned char	uchar;
typedef unsigned char	u8;
typedef unsigned short	u16;
typedef unsigned int	u32;
typedef unsigned long long  u64;
typedef signed char	s8;
typedef signed short	s16;
typedef signed int	s32;
typedef signed long long s64;

void dumphex(const char *p, u32 addr, int cnt);

#endif // DUEDUEDUE

