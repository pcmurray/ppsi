/* mqx_ppc_port.h - support PPSi under MQX with MetroWerks PPC toolchain
 * Copyright (c) 2017 by Bitronics, LLC
 * released into public domain
 */
#if defined(__MWERKS__) && defined(__POWERPC__)

#if 0
    #if __POWERPC__ == __PPCZen__
    #warning powerpc is ppczen
    #endif
#endif

//#define __noparens__(arg)  arg
//#define __attribute__(arg)  __declspec(__noparens__ arg)


#define WEAK    __declspec(weak)


#endif /* defined(__MWERKS__) && defined(__POWERPC__) */
