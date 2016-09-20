#ifndef __ARCH_H__
#define __ARCH_H__

/* we may build for HA, for testing purposes */
#include "../proto-ext-whiterabbit/wr-api.h"
#include "../proto-ext-high-accuracy/ha-api.h"

/* Architecture-specific defines, included by top-level stuff */

#include <arpa/inet.h> /* ntohs etc */
#include <stdlib.h>    /* abs */

#endif /* __ARCH_H__ */
