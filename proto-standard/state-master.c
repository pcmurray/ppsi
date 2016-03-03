/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

/* Local functions that build to nothing when Kconfig selects 0/1 vlans */
static int pp_master_issue_announce(struct pp_instance *ppi)
{
	int i, vlan = 0;

	if (CONFIG_VLAN_ARRAY_SIZE && ppi->nvlans == 1)
		vlan = ppi->vlans[0];

	if (CONFIG_VLAN_ARRAY_SIZE <= 1 || ppi->nvlans <= 1) {
		ppi->peer_vid = vlan;
		return msg_issue_announce(ppi);
	}

	/*
	 * If Kconfig selected 0/1 vlans, this code is not built.
	 * If we have several vlans, we replace peer_vid and proceed;
	 */
	for (i = 0; i < ppi->nvlans; i++) {
		ppi->peer_vid = ppi->vlans[i];
		msg_issue_announce(ppi);
		/* ignore errors: each vlan is separate */
	}
	return 0;
}

static int pp_master_issue_sync_followup(struct pp_instance *ppi)
{
	int i, vlan = 0;

	if (CONFIG_VLAN_ARRAY_SIZE && ppi->nvlans == 1)
		vlan = ppi->vlans[0];

	if (CONFIG_VLAN_ARRAY_SIZE <= 1 || ppi->nvlans <= 1) {
		ppi->peer_vid = vlan;
		return msg_issue_sync_followup(ppi);
	}

	/*
	 * If Kconfig selected 0/1 vlans, this code is not built.
	 * If we have several vlans, we replace peer_vid and proceed;
	 */
	for (i = 0; i < ppi->nvlans; i++) {
		ppi->peer_vid = ppi->vlans[i];
		msg_issue_sync_followup(ppi);
		/* ignore errors: each vlan is separate */
	}
	return 0;
}


/* The real state function, relying on the two above for sending */
int pp_master(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	int msgtype, d1, d2;
	int e = 0; /* error var, to check errors in msg handling */
	MsgHeader *hdr = &ppi->received_ptp_header;
	MsgPDelayRespFollowUp respFllw;

	if (ppi->is_new_state) {
		pp_timeout_set(ppi, PP_TO_SYNC_SEND);
		pp_timeout_set(ppi, PP_TO_REQUEST);
		pp_timeout_set(ppi, PP_TO_ANN_SEND);

		/* Send an announce immediately, when becomes master */
		if ((e = pp_master_issue_announce(ppi)) < 0)
			goto out;
	}

	if (pp_timeout_z(ppi, PP_TO_SYNC_SEND)) {
		/* Restart the timeout for next time */
		pp_timeout_set(ppi, PP_TO_SYNC_SEND);

		if ((e = pp_master_issue_sync_followup(ppi) < 0))
			goto out;

	}

	if (pp_timeout_z(ppi, PP_TO_ANN_SEND)) {
		if ((e = pp_master_issue_announce(ppi) < 0))
			goto out;

		/* Restart the timeout for next time */
		pp_timeout_set(ppi, PP_TO_ANN_SEND);
	}

	/* when the clock is using peer-delay, the muster mast send it too */
	if (ppi->glbs->delay_mech == PP_P2P_MECH
	    && pp_timeout_z(ppi, PP_TO_REQUEST)) {
		e = msg_issue_request(ppi);

		ppi->t3 = ppi->last_snt_time;

		/* Restart the timeout for next time */
		pp_timeout_set(ppi, PP_TO_REQUEST);
	}

	if (plen == 0)
		goto out;

	/*
	 * An extension can do special treatment of this message type,
	 * possibly returning error or eating the message by returning
	 * PPM_NOTHING_TO_DO
	 */
	msgtype = ppi->received_ptp_header.messageType;
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

	d1 = pp_ms_to_timeout(ppi, PP_TO_ANN_SEND);
	d2 = pp_ms_to_timeout(ppi, PP_TO_SYNC_SEND);
	ppi->next_delay = d1 < d2 ? d1 : d2;
	return e;

out_fault:
	ppi->next_state = PPS_FAULTY;
	ppi->next_delay = 500; /* just a delay to releif the system */
	return e;
}
