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

	if (pp_hooks.listening)
		e = pp_hooks.listening(ppi, pkt, plen);
	if (e)
		goto out;

	/* when the clock is using peer-delay, listening must send it too */
	if (CONFIG_HAS_P2P && ppi->mech == PP_P2P_MECH)
		e  = pp_lib_may_issue_request(ppi);

	if (plen == 0)
		goto out;

	switch (ppi->received_ptp_header.messageType) {

	case PPM_ANNOUNCE:
		e = st_com_master_handle_announce(ppi, pkt, plen);
		break;

	case PPM_SYNC:
		e = st_com_master_handle_sync(ppi, pkt, plen);
		break;

	case PPM_PDELAY_REQ:
		if (CONFIG_HAS_P2P)
			st_com_peer_handle_preq(ppi, pkt, plen);
		break;

	case PPM_PDELAY_RESP:
		if (CONFIG_HAS_P2P)
			e = st_com_peer_handle_pres(ppi, pkt, plen);
		break;

	case PPM_PDELAY_R_FUP:
		if (CONFIG_HAS_P2P)
			e = st_com_peer_handle_pres_followup(ppi, pkt, plen);
		break;

	case PPM_SIGNALING:
		if (pp_hooks.handle_signaling)
			e = pp_hooks.handle_signaling(ppi, pkt, plen);

	default:
		/* disregard, nothing to do */
		break;
	}

out:
	if (e == 0)
		e = st_com_execute_slave(ppi);

	if (e != 0)
		ppi->next_state = PPS_FAULTY;

	ppi->next_delay = pp_next_delay_1(ppi, PP_TO_ANN_RECEIPT);

	if (pp_hooks.calc_timeout) {
		/* The extension may manage its own timeout, and do stuff */
		int ext_to = pp_hooks.calc_timeout(ppi);
		if (ext_to < ppi->next_delay)
			ppi->next_delay = ext_to;
	}

	return 0;
}
