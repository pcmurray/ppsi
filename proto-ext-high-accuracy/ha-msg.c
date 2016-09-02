
/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on ptp-noposix project (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "ha-api.h"

int ha_pack_signal(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	void *buf;

	buf = ppi->tx_ptp;

	/* Changes in header */
	*(char *)(buf+0) = *(char *)(buf+0) & 0xF0; /* RAZ messageType */
	*(char *)(buf+0) = *(char *)(buf+0) | 0x0C; /* Table 19 -> signaling */

	*(UInteger8 *)(buf+32) = 0x05; //Table 23 -> all other

	/* target portIdentity */
	memcpy((buf+34), &DSPAR(ppi)->parentPortIdentity.clockIdentity,
		PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *)(buf + 42) =
		htons(DSPAR(ppi)->parentPortIdentity.portNumber);

	/* L1SyncTLV */
	*(UInteger16 *)(buf+44) = htons(TLV_TYPE_L1_SYNC);
	*(UInteger16 *)(buf+46) = htons(2); /* len */
	/* O.6.4 */
	*(UInteger8 *)(buf+48) = wrp->ha_conf;
	*(UInteger8 *)(buf+49) = wrp->ha_active;
	pp_diag(ppi, ext, 2, "send configured %02x, active %02x\n",
		wrp->ha_conf, wrp->ha_active);

	/* header len */
	*(UInteger16 *)(buf + 2) = htons(50);

	return 50;
}

int ha_unpack_signal(struct pp_instance *ppi, void *pkt, int plen)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);

	if ((*(char *)(pkt+0) & 0x0f) != 0x0c) {
		pp_diag(ppi, ext, 1, "Not a signaling message, ignore\n");
		return -1;
	}
	if (*(UInteger16 *)(pkt+44) != htons(TLV_TYPE_L1_SYNC)) {
		pp_diag(ppi, ext, 1, "Not L1Sync TLV, ignore\n");
		return -1;
	}
	if (*(UInteger16 *)(pkt+46) != htons(2) || plen != 50) {
		pp_diag(ppi, ext, 1, "L1Sync TLV wrong length, ignore\n");
		return -1;
	}
	wrp->ha_peer_conf = *(UInteger8 *)(pkt+48);
	wrp->ha_peer_active = *(UInteger8 *)(pkt+49);
	pp_diag(ppi, ext, 2, "recv configured %02x, active %02x\n",
		wrp->ha_peer_conf, wrp->ha_peer_active);

	/* And now (FIXME) move the local state machine (O.7.3, O.7.4) */

	return 0;
}
