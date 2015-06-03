#ifndef __ARCH_H__
#define __ARCH_H__

/* Architecture-specific defines, included by top-level stuff */

/* please note that these have multiple evaluation of the argument */
#define	htons(x)	__bswap_16(x)
#define	__bswap_16(x)	((((x) >> 8) & 0xff) | (((x) & 0xff) << 8))

#define htonl(x)	__bswap_32(x)
#define __bswap_32(x)	(					\
	(((uint32_t)(x) & (uint32_t)0x000000ffUL) << 24) |	\
	(((uint32_t)(x) & (uint32_t)0x0000ff00UL) <<  8) |	\
	(((uint32_t)(x) & (uint32_t)0x00ff0000UL) >>  8) |	\
	(((uint32_t)(x) & (uint32_t)0xff000000UL) >> 24))

#  define htobe64(x) __bswap_64 (x)
#define __bswap_constant_64(x) \
     ((((uint64_t)(x)  &  (uint64_t)0xff00000000000000ul)>> 56)		|	\
       (((uint64_t)(x) &  (uint64_t)0x00ff000000000000ul)>>	40)		|	\
       (((uint64_t)(x) &  (uint64_t)0x0000ff0000000000ul)>> 24)		|	\
       (((uint64_t)(x) &  (uint64_t)0x000000ff00000000ul)>>  8)		|	\
       (((uint64_t)(x) &  (uint64_t)0x00000000ff000000ul)<<  8)		|	\
       (((uint64_t)(x) &  (uint64_t)0x0000000000ff0000ul)<< 24)		|	\
       (((uint64_t)(x) &  (uint64_t)0x000000000000ff00ul)<< 40)		|	\
       (((uint64_t)(x) &  (uint64_t)0x00000000000000fful)<< 56))

#define ntohs htons
#define ntohl htonl

#define abs(x) ((x >= 0) ? x : -x)
#endif /* __ARCH_H__ */
