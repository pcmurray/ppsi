/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_master(struct pp_instance *ppi, void *pkt, int plen)
{
	int msgtype;
	int e = 0; /* error var, to check errors in msg handling */
	struct msg_header_wire *hdr = &ppi->received_ptp_header;
	MsgPDelayRespFollowUp respFllw;

	/* ignore errors; we are not getting FAULTY if not transmitting */
	pp_lib_may_issue_sync(ppi);
	pp_lib_may_issue_announce(ppi);

	/* when the clock is using peer-delay, the muster mast send it too */
	if (ppi->glbs->delay_mech == PP_P2P_MECH)
		pp_lib_may_issue_request(ppi);
	else
		pp_timeout_set(ppi, PP_TO_REQUEST);

	if (plen == 0)
		goto out;

	/*
	 * An extension can do special treatment of this message type,
	 * possibly returning error or eating the message by returning
	 * PPM_NOTHING_TO_DO
	 */
	msgtype = msg_hdr_get_msg_type(&ppi->received_ptp_header);
	if (pp_hooks.master_msg)
		msgtype = pp_hooks.master_msg(ppi, pkt, plen, msgtype);
	if (msgtype < 0) {
		e = msgtype;
		goto out_fault;
	}

	switch (msgtype) {

	case PPM_NOTHING_TO_DO:
		break;

	case PPM_ANNOUNCE:
		e = st_com_master_handle_announce(ppi, pkt, plen);
		break;

	case PPM_SYNC:
		e = st_com_master_handle_sync(ppi, pkt, plen);
		break;

	case PPM_DELAY_REQ:
		msg_issue_delay_resp(ppi, &ppi->last_rcv_time);
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
			if (e)
				goto out;

		} else {
			pp_diag(ppi, frames, 2, "%s: "
				"PDelay Resp F-up doesn't match PDelay Req\n",
				__func__);
		}
		break;

	default:
		/* disregard, nothing to do */
		break;
	}

out:
	switch(e) {
	case PP_SEND_OK: /* 0 */
		if (DSDEF(ppi)->clockQuality.clockClass == PP_CLASS_SLAVE_ONLY
		    || (ppi->role == PPSI_ROLE_SLAVE))
			ppi->next_state = PPS_LISTENING;
		break;
	case PP_SEND_ERROR:
		goto out_fault;

	case PP_SEND_NO_STAMP:
		/* nothing, just keep the ball rolling */
		e = 0;
		break;
	}

	ppi->next_delay = pp_next_delay_3(ppi,
		PP_TO_ANN_SEND, PP_TO_SYNC_SEND, PP_TO_REQUEST);
	return e;

out_fault:
	ppi->next_state = PPS_FAULTY;
	ppi->next_delay = 500; /* just a delay to releif the system */
	return e;
}
