/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_listening(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	int e = 0; /* error var, to check errors in msg handling */
	
	struct wr_dsport *wrp_hsr0 = WR_DSPOR(INST(ppi->glbs, 0));
	struct wr_dsport *wrp_hsr1 = WR_DSPOR(INST(ppi->glbs, 1));
	struct wr_dsport *wrp_hsr2 = WR_DSPOR(INST(ppi->glbs, 2));

	if(wrp_hsr0->wrModeOn || wrp_hsr1->wrModeOn || wrp_hsr2->wrModeOn) {
		ppi->master_only = 1;
		ppi->slave_only = 0;
		ppi->backup_only = 0;
	}

	if (pp_hooks.listening)
		e = pp_hooks.listening(ppi, pkt, plen);
	if (e)
		goto out;

	if (ppi->is_new_state)
		pp_timeout_restart_annrec(ppi);

	if (plen == 0)
		goto out;

	switch (ppi->received_ptp_header.messageType) {

	case PPM_ANNOUNCE:
		e = st_com_master_handle_announce(ppi, pkt, plen);
		break;

	case PPM_SYNC:
		e = st_com_master_handle_sync(ppi, pkt, plen);
		break;
		
	case PPM_PDELAY_REQ: /* GUTI HACK */
		e = (plen < PP_PDELAY_RESP_LENGTH);
		if (e)
			break;

		if (pp_hooks.handle_preq)
			e = pp_hooks.handle_preq(ppi);
		else
			e = st_com_peer_handle_preq(ppi, pkt, plen);

		if (e)
			goto out;

		break;

	default:
		/* disregard, nothing to do */
		break;
	}

out:
	if (e == 0)
		e = st_com_execute_slave(ppi);

	if (e != 0)
		ppi->next_state = PPS_FAULTY;

	/* Leaving this state */
	if (ppi->next_state != ppi->state)
		pp_timeout_clr(ppi, PP_TO_ANN_RECEIPT);

	ppi->next_delay = pp_ms_to_timeout(ppi, PP_TO_ANN_RECEIPT);

	return 0;
}
