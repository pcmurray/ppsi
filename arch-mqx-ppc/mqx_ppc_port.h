/* multi_toolchain.h -  ppsi multi-toolchain support
 * Copyright (c) 2017 by Bitronics, LLC
 * released into public domain
 */
#ifndef MULTI_TOOLCHAIN_H
#define MULTI_TOOLCHAIN_H

/* provide compiler-independent definitions for
 *   GCC's weak and packed attributes
 */

#if COMPILER_GCC
#define WEAK        __attribute__((weak))
#define PACK_START  __attribute__((packed))
#define PACK_END    /* unused for GCC */   
#endif /* COMPILER_GCC */


#if defined(__MWERKS__) && defined(__POWERPC__)
/* MetroWerks PPC toolchain */
#define WEAK        __declspec(weak)
#define PACK_START  _Pragma("pack(push, 1)")
#define PACK_END    _Pragma("pack(pop)")
#endif /* defined(__MWERKS__) && defined(__POWERPC__) */


#endif /* MULTI_TOOLCHAIN_H */
