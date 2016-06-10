/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_passive(struct pp_instance *ppi, void *pkt, int plen)
{
	int e = 0; /* error var, to check errors in msg handling */

	/* when the clock is using peer-delay, listening must send it too */
	if (ppi->glbs->delay_mech == PP_P2P_MECH)
		e  = pp_lib_may_issue_request(ppi);

	if (plen == 0)
		goto no_incoming_msg;

	switch (msg_hdr_get_msg_type(&ppi->received_ptp_header))  {

	case PPM_ANNOUNCE:
		e = st_com_master_handle_announce(ppi, pkt, plen);
		break;

	case PPM_SYNC:
		e = st_com_master_handle_sync(ppi, pkt, plen);
		break;

	case PPM_PDELAY_REQ:
		st_com_peer_handle_preq(ppi, pkt, plen);
		break;

	case PPM_PDELAY_RESP:
		e = st_com_peer_handle_pres(ppi, pkt, plen);
		break;

	case PPM_PDELAY_RESP_FOLLOW_UP:
		e = st_com_peer_handle_pres_followup(ppi, pkt, plen);
		break;
	default:
		/* disreguard, nothing to do */
		break;

	}

no_incoming_msg:
	if (e == 0)
		e = st_com_execute_slave(ppi);

	if (e != 0)
		ppi->next_state = PPS_FAULTY;

	ppi->next_delay = PP_DEFAULT_NEXT_DELAY_MS;

	return 0;
}
