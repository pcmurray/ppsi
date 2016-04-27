/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_passive(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	int e = 0; /* error var, to check errors in msg handling */
	struct msg_header_wire *hdr = &ppi->received_ptp_header;
	MsgPDelayRespFollowUp respFllw;

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
		if (plen < PP_PDELAY_RESP_FOLLOW_UP_LENGTH)
			break;

		msg_unpack_pdelay_resp_follow_up(pkt, &respFllw);

		if ((memcmp(&DSPOR(ppi)->portIdentity.clockIdentity,
			    &respFllw.requestingPortIdentity.clockIdentity,
			    PP_CLOCK_IDENTITY_LENGTH) == 0) &&
		    ((ppi->sent_seq[PPM_PDELAY_REQ]) ==
		     msg_hdr_get_msg_seq_id(hdr)) &&
		    (DSPOR(ppi)->portIdentity.portNumber ==
		     respFllw.requestingPortIdentity.portNumber) &&
		    (ppi->flags & PPI_FLAG_FROM_CURRENT_PARENT)) {

			to_TimeInternal(&ppi->t5,
					&respFllw.responseOriginTimestamp);
			ppi->flags |= PPI_FLAG_WAITING_FOR_RF_UP;

			if (pp_hooks.handle_presp)
				e = pp_hooks.handle_presp(ppi);
			else
				pp_servo_got_presp(ppi);

		} else {
			pp_diag(ppi, frames, 2, "%s: "
				"PDelay Resp F-up doesn't match PDelay Req\n",
				__func__);
		}
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
