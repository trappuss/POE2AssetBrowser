// Portable stdafx.h for the vendored ooz decoder (powzix/ooz).
//
// The upstream ooz targets MSVC + Windows. This replacement lets the SAME unmodified kraken.cpp /
// bitknit.cpp / lzna.cpp compile with MSVC (the user's build) AND with GCC/Clang (the container's
// verification build), so the decoder is checked on the machine that writes it and shipped to the
// machine that runs it. Only this header and the CMake `-Dmain=ooz_cli_main` rename differ from
// upstream; the three .cpp files are byte-for-byte the release.
#pragma once

// MSVC: the UCRT only defines the POSIX names `struct stat` / `stat()` when
// _CRT_DECLARE_NONSTDC_NAMES is on, and this toolset (or a build define) leaves it off — so
// <sys/stat.h> merely forward-declares `struct stat`, and kraken.cpp's CLI tail (line ~4350,
// `struct stat sb;`) fails with C2079 "uses undefined struct". Force the names on. This MUST come
// before the first CRT header below, because <corecrt.h> (pulled in by <stdio.h>) latches the
// value; putting the <sys/stat.h> include alone after <stdio.h> was not enough.
#ifdef _MSC_VER
  #undef  _CRT_DECLARE_NONSTDC_NAMES
  #define _CRT_DECLARE_NONSTDC_NAMES 1
  #define _CRT_NONSTDC_NO_WARNINGS 1
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <stdint.h>
#include <emmintrin.h>
#include <xmmintrin.h>

#ifdef _MSC_VER
  // Native MSVC/Windows: use the real headers, exactly as upstream did.
  #include <intrin.h>
  #include <windows.h>
  #include <sys/stat.h>   // struct stat / stat() used by the CLI tail of kraken.cpp; windows.h does
                          // not pull it in, and g++ only built without it because it reached
                          // <sys/stat.h> transitively (the #else branch includes it explicitly).
  #pragma warning (disable: 4244)
#else
  // GCC/Clang shims for the MSVC intrinsics and the few Win32 calls the CLI tail of kraken.cpp uses.
  #include <immintrin.h>
  #include <sys/stat.h>
  #define __forceinline inline __attribute__((always_inline))
  #ifndef WINAPI
    #define WINAPI
  #endif
  typedef int64_t __int64;
  typedef void* HINSTANCE;
  typedef union { struct { uint32_t LowPart; int32_t HighPart; }; int64_t QuadPart; } LARGE_INTEGER;
  static inline unsigned char _BitScanReverse(unsigned long* index, unsigned long mask) {
      if (!mask) return 0; *index = 31 - __builtin_clz(mask); return 1; }
  static inline unsigned char _BitScanForward(unsigned long* index, unsigned long mask) {
      if (!mask) return 0; *index = __builtin_ctz(mask); return 1; }
  static inline unsigned long  _byteswap_ulong(unsigned long v)  { return __builtin_bswap32((uint32_t)v); }
  static inline unsigned int   _byteswap_uint(unsigned int v)    { return __builtin_bswap32(v); }
  static inline uint64_t       _byteswap_uint64(uint64_t v)      { return __builtin_bswap64(v); }
  static inline unsigned short _byteswap_ushort(unsigned short v){ return __builtin_bswap16(v); }
  #ifndef _rotl
  static inline unsigned int _rotl(unsigned int v, int s) { return (v << s) | (v >> (32 - s)); }
  #endif
  static inline HINSTANCE LoadLibraryA(const char*) { return 0; }
  static inline void* GetProcAddress(HINSTANCE, const char*) { return 0; }
  static inline int QueryPerformanceCounter(LARGE_INTEGER* v) { v->QuadPart = 0; return 1; }
  static inline int QueryPerformanceFrequency(LARGE_INTEGER* v) { v->QuadPart = 1; return 1; }
#endif

typedef unsigned char  byte;
typedef unsigned char  uint8;
typedef unsigned int   uint32;
typedef uint64_t       uint64;
typedef int64_t        int64;
typedef signed int     int32;
typedef unsigned short uint16;
typedef signed short   int16;
typedef unsigned int   uint;
