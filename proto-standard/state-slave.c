/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Copyright (C) 2014 GSI (www.gsi.de)
 * Author: Cesar Prados
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_slave(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	int e = 0; /* error var, to check errors in msg handling */
	struct msg_header_wire *hdr = &ppi->received_ptp_header;
	MsgDelayResp resp;
	MsgPDelayRespFollowUp respFllw;

	if (ppi->is_new_state) {
		memset(&ppi->t1, 0, sizeof(ppi->t1));
		pp_servo_init(ppi);

		if (pp_hooks.new_slave)
			e = pp_hooks.new_slave(ppi, pkt, plen);
		if (e)
			goto out;
	}

	e  = pp_lib_may_issue_request(ppi);

	if (plen == 0)
		goto out;

	switch (msg_hdr_get_msg_type(hdr)) {

	case PPM_ANNOUNCE:
		e = st_com_slave_handle_announce(ppi, pkt, plen);
		break;

	case PPM_SYNC:
		e = st_com_slave_handle_sync(ppi, pkt, plen);
		break;

	case PPM_FOLLOW_UP:
		e = st_com_slave_handle_followup(ppi, pkt, plen);
		break;

	case PPM_DELAY_REQ:
		/* Being slave, we are not waiting for a delay request */
		break;

	case PPM_DELAY_RESP:
		if (plen < PP_DELAY_RESP_LENGTH)
			break;

		msg_unpack_delay_resp(pkt, &resp);

		if ((memcmp(&DSPOR(ppi)->portIdentity.clockIdentity,
			&resp.requestingPortIdentity.clockIdentity,
			PP_CLOCK_IDENTITY_LENGTH) == 0) &&
			((ppi->sent_seq[PPM_DELAY_REQ]) ==
			 msg_hdr_get_msg_seq_id(hdr)) &&
			(DSPOR(ppi)->portIdentity.portNumber ==
			resp.requestingPortIdentity.portNumber)
			&& (ppi->flags & PPI_FLAG_FROM_CURRENT_PARENT)) {

			to_TimeInternal(&ppi->t4, &resp.receiveTimestamp);

			/*
			 * FIXME: how is correctionField handled in t3/t4?
			 * I think the master should consider it when
			 * generating t4, and report back a modified t4
			 */

			if (pp_hooks.handle_resp)
				e = pp_hooks.handle_resp(ppi);
			else
				pp_servo_got_resp(ppi);
			if (e)
				goto out;

			DSPOR(ppi)->logMinDelayReqInterval =
				msg_hdr_get_log_msg_intvl(hdr);
			pp_timeout_init(ppi); /* new value for logMin */
		} else {
			pp_diag(ppi, frames, 2, "pp_slave : "
			     "Delay Resp doesn't match Delay Req\n");
		}

		break;

	case PPM_PDELAY_REQ:
		e = st_com_peer_handle_preq(ppi, pkt, plen);
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
			if (e)
				goto out;

		} else {
			pp_diag(ppi, frames, 2, "pp_pclock : "
				"PDelay Resp F-up doesn't match PDelay Req\n");
		}
		break;

	default:
		/* disregard, nothing to do */
		break;
	}

out:
	if (e == 0)
		e = st_com_execute_slave(ppi);

	switch(e) {
	case PP_SEND_OK: /* 0 */
		break;
	case PP_SEND_ERROR:
		ppi->next_state = PPS_FAULTY;
		break;
	case PP_SEND_NO_STAMP:
		/* nothing, just keep the ball rolling */
		e = 0;
		break;
	}

	if (ppi->next_state != ppi->state) {
		pp_servo_init(ppi);
		return e;
	}
	ppi->next_delay = pp_next_delay_2(ppi,
					  PP_TO_ANN_RECEIPT, PP_TO_REQUEST);
	return e;
}
