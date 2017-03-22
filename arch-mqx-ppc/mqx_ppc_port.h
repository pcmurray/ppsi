/* mqx_ppc_port.h - utilities to support PPSi under MQX with MetroWerks PPC
 * Copyright (c) 2017 by Bitronics, LLC
 * released into public domain
 */
#if defined(__MWERKS__) && defined(__POWERPC__)

#if 0
    #if __POWERPC__ == __PPCZen__
    #warning powerpc is ppczen
    #endif
#endif

#define WEAK    __declspec(weak)


#endif /* defined(__MWERKS__) && defined(__POWERPC__) */
