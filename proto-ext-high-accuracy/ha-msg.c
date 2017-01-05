
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
	uint8_t local_config, local_active;

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
	local_config = ha_L1Sync_creat_bitmask(ppi->L1BasicDS->txCoherentConfigured,
	                                       ppi->L1BasicDS->rxCoherentConfigured,
	                                       ppi->L1BasicDS->congruentConfigured);
	if(ppi->L1BasicDS->optParamsEnabled)
		  local_config |= HA_OPT_PARAMS;
	
	local_active = ha_L1Sync_creat_bitmask(ppi->L1BasicDS->txCoherentActive,
	                                       ppi->L1BasicDS->rxCoherentActive,
	                                       ppi->L1BasicDS->congruentActive);
	*(UInteger8 *)(buf+48) = local_config;
	*(UInteger8 *)(buf+49) = local_active;

	ha_print_L1Sync_basic_bitmaps(ppi, local_config,local_active, "Sent");

	/* header len */
	*(UInteger16 *)(buf + 2) = htons(50);

	return 50;
}

int ha_unpack_signal(struct pp_instance *ppi, void *pkt, int plen)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	int ha_peer_conf, ha_peer_acti;
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
	ha_peer_conf = *(UInteger8 *)(pkt+48);
	ha_peer_acti = *(UInteger8 *)(pkt+49);

	ha_print_L1Sync_basic_bitmaps(ppi, ha_peer_conf,ha_peer_acti, "Received");

	ppi->L1BasicDS->peerTxCoherentConfigured = (ha_peer_conf & HA_TX_COHERENT) == HA_TX_COHERENT;
	ppi->L1BasicDS->peerRxCoherentConfigured = (ha_peer_conf & HA_RX_COHERENT) == HA_RX_COHERENT;
	ppi->L1BasicDS->peerCongruentConfigured  = (ha_peer_conf & HA_CONGRUENT)   == HA_CONGRUENT;

	ppi->L1BasicDS->peerTxCoherentActive     = (ha_peer_acti & HA_TX_COHERENT) == HA_TX_COHERENT;
	ppi->L1BasicDS->peerRxCoherentActive     = (ha_peer_acti & HA_RX_COHERENT) == HA_RX_COHERENT;
	ppi->L1BasicDS->peerCongruentActive      = (ha_peer_acti & HA_CONGRUENT)   == HA_CONGRUENT;
	/* And now (FIXME) move the local state machine (O.7.3, O.7.4) */

	return 0;
}
