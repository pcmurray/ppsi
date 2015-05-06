/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_master(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	TimeInternal *time_snt;
	int msgtype, d1, d2;
	int e = 0; /* error var, to check errors in msg handling */

	/* forwarding is the first priority */
	if (ppi->fwd_sync_flag) { /* forward sync */
		memcpy(ppi->tx_backup, ppi->tx_buffer,
			PP_MAX_FRAME_LENGTH);
		memcpy(ppi->tx_buffer, ppi->fwd_sync_buffer,
			PP_MAX_FRAME_LENGTH);
		__send_and_log(ppi, PP_SYNC_LENGTH, PPM_SYNC, PP_NP_EVT);
		memcpy(ppi->tx_buffer, ppi->tx_backup,
			PP_MAX_FRAME_LENGTH);
		ppi->fwd_sync_flag = 0;
	}
	if (ppi->fwd_fup_flag) { /* forward follow_up */
		memcpy(ppi->tx_backup, ppi->tx_buffer,
			PP_MAX_FRAME_LENGTH);
		memcpy(ppi->tx_buffer, ppi->fwd_fup_buffer,
			PP_MAX_FRAME_LENGTH);
		 __send_and_log(ppi, PP_FOLLOW_UP_LENGTH, PPM_FOLLOW_UP,
				PP_NP_EVT);
		memcpy(ppi->tx_buffer, ppi->tx_backup,
			PP_MAX_FRAME_LENGTH);
		ppi->fwd_fup_flag = 0;
	}
	/* end of forwarding */
	
	if (ppi->is_new_state) {
		pp_timeout_rand(ppi, PP_TO_SYNC, DSPOR(ppi)->logSyncInterval);
		pp_timeout_rand(ppi, PP_TO_ANN_INTERVAL,
				DSPOR(ppi)->logAnnounceInterval);

		/* Send an announce immediately, when becomes master */
		if ((e = msg_issue_announce(ppi)) < 0)
			goto out;
	}

	if (pp_timeout_z(ppi, PP_TO_SYNC)) {
		if ((e = msg_issue_sync(ppi) < 0))
			goto out;

		time_snt = &ppi->last_snt_time;
		add_TimeInternal(time_snt, time_snt,
				 &OPTS(ppi)->outbound_latency);
		if ((e = msg_issue_followup(ppi, time_snt)))
			goto out;

		/* Restart the timeout for next time */
		pp_timeout_rand(ppi, PP_TO_SYNC, DSPOR(ppi)->logSyncInterval);
	}

	if (pp_timeout_z(ppi, PP_TO_ANN_INTERVAL)) {
		if ((e = msg_issue_announce(ppi) < 0))
			goto out;

		/* Restart the timeout for next time */
		pp_timeout_rand(ppi, PP_TO_ANN_INTERVAL,
				DSPOR(ppi)->logAnnounceInterval);
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
		goto out;
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
		msg_copy_header(&ppi->delay_req_hdr,
				&ppi->received_ptp_header);
		msg_issue_delay_resp(ppi, &ppi->last_rcv_time);
		break;

        case PPM_PDELAY_REQ:
                msg_copy_header(&ppi->pdelay_req_hdr,
                                &ppi->received_ptp_header);
                msg_issue_pdelay_resp(ppi, &ppi->last_rcv_time);
                msg_issue_pdelay_resp_followup(ppi, &ppi->last_snt_time);
                break;

	default:
		/* disregard, nothing to do */
		break;
	}

out:
	if (e == 0) {
		if (DSDEF(ppi)->clockQuality.clockClass == PP_CLASS_SLAVE_ONLY
		    || ppi->slave_only)
			ppi->next_state = PPS_LISTENING;
	} else {
		ppi->next_state = PPS_FAULTY;
	}

	d1 = pp_ms_to_timeout(ppi, PP_TO_ANN_INTERVAL);
	d2 = pp_ms_to_timeout(ppi, PP_TO_SYNC);
	ppi->next_delay = d1 < d2 ? d1 : d2;
	return 0;
}
