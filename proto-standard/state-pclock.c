/*
 * Copyright (C) 2014 GSI (www.gsi.de)
 * Author: Cesar Prados
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_pclock(struct pp_instance *ppi, unsigned char *pkt, int plen)
{

	int e = 0;		/* error var, to check errors in msg handling */
	MsgHeader *hdr = &ppi->received_ptp_header;
	MsgPDelayRespFollowUp respFllw;
	int d1, d2;

	if (ppi->is_new_state) {
		pp_servo_init(ppi);

		if (pp_hooks.new_slave)
			e = pp_hooks.new_slave(ppi, pkt, plen);
		if (e)
			goto out;

		ppi->flags &= ~PPI_FLAGS_WAITING;

		pp_timeout_restart_annrec(ppi);
		pp_timeout_rand(ppi, PP_TO_DELAYREQ,
				DSPOR(ppi)->logMinDelayReqInterval);
	}

	if (plen == 0)
		goto out;

	switch (hdr->messageType) {

	case PPM_ANNOUNCE:
		e = st_com_slave_handle_announce(ppi, pkt, plen);
		break;

	case PPM_SYNC:
		e = st_com_slave_handle_sync(ppi, pkt, plen);
		break;

	case PPM_FOLLOW_UP:
		e = st_com_slave_handle_followup(ppi, pkt, plen);
		break;

	case PPM_PDELAY_REQ:
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

	case PPM_PDELAY_RESP:

		e = st_com_peer_handle_pres(ppi, pkt, plen);
		break;

	case PPM_PDELAY_RESP_FOLLOW_UP:
		e = (plen < PP_PDELAY_RESP_FOLLOW_UP_LENGTH);
		if (e)
			break;

		msg_unpack_pdelay_resp_follow_up(pkt, &respFllw);

		if ((memcmp(&DSPOR(ppi)->portIdentity.clockIdentity,
			    &respFllw.requestingPortIdentity.clockIdentity,
			    PP_CLOCK_IDENTITY_LENGTH) == 0) &&
		    ((ppi->sent_seq[PPM_PDELAY_REQ]) ==
		     hdr->sequenceId) &&
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
			if (e)
				goto out;

		} else {
			pp_diag(ppi, frames, 2, "pp_pclock : "
				"PDelay Resp Follow doesn't match PDelay Req\n");
		}
		break;

	default:
		/* disregard, nothing to do */
		break;
	}

out:
	if (e == 0)
		e = st_com_execute_slave(ppi);

	if (pp_timeout_z(ppi, PP_TO_DELAYREQ)) {
		e = msg_issue_pdelay_req(ppi);

		ppi->t3 = ppi->last_snt_time;

		/* Restart the timeout for next time */
		pp_timeout_rand(ppi, PP_TO_DELAYREQ,
				DSPOR(ppi)->logMinDelayReqInterval);
	}

	if (e) {
		ppi->next_state = PPS_FAULTY;
		return 0;
	}

	/* Leaving this state */
	if (ppi->next_state != ppi->state) {
		pp_timeout_clr(ppi, PP_TO_ANN_RECEIPT);
		pp_timeout_clr(ppi, PP_TO_DELAYREQ);

		pp_servo_init(ppi);
	}

	d1 = d2 = pp_ms_to_timeout(ppi, PP_TO_ANN_RECEIPT);
	if (ppi->timeouts[PP_TO_DELAYREQ])
		d2 = pp_ms_to_timeout(ppi, PP_TO_DELAYREQ);

	ppi->next_delay = d1 < d2 ? d1 : d2;

	return 0;
}
