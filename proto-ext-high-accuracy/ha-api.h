/*
 * Copyright (C) 2016 CERN (www.cern.ch)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#ifndef __HAEXT_HA_API_H__
#define __HAEXT_HA_API_H__

#include <ppsi/lib.h>

/* We are still very dependent on whiterabbit code, so include wr_ops etc */
#include "../proto-ext-whiterabbit/wr-api.h"

/* Rename the timeouts, for readability */
#define HA_TO_TX	PP_TO_EXT_0
#define HA_TO_RX	PP_TO_EXT_1
#define HA_TO_LOCK	PP_TO_EXT_2

#define HA_DEFAULT_L1SYNCRECEIPTTIMEOUT 5 /* was 3: need more for pll lock */

#define  TLV_TYPE_L1_SYNC		0x8001

/*
 * We don't have ha_dsport, but rather rely on wr_dsport with our fields added.
 *
 *
 * Fortunately (and strangely (thanks Maciej), here the spec *is* consistent.
 * These 3 bits are valid 4 times, in 4 different bytes (conf/active, me/peer)
 */
#define HA_TX_COHERENT	0x01
#define HA_RX_COHERENT	0x02
#define HA_CONGRUENT	0x04

enum l1_sync_states { /* O.5.3.5 */
	__L1SYNC_MISSING = 0, /* my addition... */
	L1SYNC_DISABLED = 1,
	L1SYNC_IDLE,
	L1SYNC_LINK_ALIVE,
	L1SYNC_CONFIG_MATCH,
	L1SYNC_UP,
};

/* Pack/Unkpack HA messages (all of them are signalling messages) */
int ha_pack_signal(struct pp_instance *ppi);
int ha_unpack_signal(struct pp_instance *ppi, void *pkt, int plen);

#endif /* __HAEXT_HA_API_H__ */
