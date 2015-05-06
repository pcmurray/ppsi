/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_slave(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	int e = 0; /* error var, to check errors in msg handling */
	MsgHeader *hdr = &ppi->received_ptp_header;
	MsgDelayResp resp;
	int d1, d2;

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
		pp_servo_init(ppi);

		if (pp_hooks.new_slave)
			e = pp_hooks.new_slave(ppi, pkt, plen);
		if (e)
			goto out;

		ppi->waiting_for_follow = FALSE;

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
		/* forward before is processed by slave */
		memcpy(INST(ppi->glbs, 2)->fwd_sync_buffer, ppi->rx_buffer,
				PP_MAX_FRAME_LENGTH);
		INST(ppi->glbs, 2)->fwd_sync_flag = 1; // master is in charge of = 0
		/* end of forwarding */
		e = st_com_slave_handle_sync(ppi, pkt, plen);
		
		break;

	case PPM_FOLLOW_UP:
		/* forward before is processed by slave */
		memcpy(INST(ppi->glbs, 2)->fwd_fup_buffer, ppi->rx_buffer,
				PP_MAX_FRAME_LENGTH);
		INST(ppi->glbs, 2)->fwd_fup_flag = 1; // master is in charge of = 0
		/* end of forwarding */
		e = st_com_slave_handle_followup(ppi, pkt, plen);
		break;

	case PPM_DELAY_REQ:
		/* Being slave, we are not waiting for a delay request */
		break;

	case PPM_DELAY_RESP:

		e = (plen < PP_DELAY_RESP_LENGTH);

		if (e)
			break;

		msg_unpack_delay_resp(pkt, &resp);

		if ((memcmp(&DSPOR(ppi)->portIdentity.clockIdentity,
			&resp.requestingPortIdentity.clockIdentity,
			PP_CLOCK_IDENTITY_LENGTH) == 0) &&
			((ppi->sent_seq[PPM_DELAY_REQ]) ==
				hdr->sequenceId) &&
			(DSPOR(ppi)->portIdentity.portNumber ==
			resp.requestingPortIdentity.portNumber)
			&& ppi->is_from_cur_par) {

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

			ppi->log_min_delay_req_interval =
				hdr->logMessageInterval;

		} else {
			pp_diag(ppi, frames, 2, "pp_slave : "
			     "Delay Resp doesn't match Delay Req\n");
		}

		break;

	/*
	 * We are not supporting pdelay (not configured to, see
	 * 9.5.13.1, p 106), so all the code about pdelay is removed
	 * as a whole by one commit in our history. It can be recoverd
	 * and fixed if needed
	 */

	default:
		/* disregard, nothing to do */
		break;

	}

out:
	if (e == 0)
		e = st_com_execute_slave(ppi);

	if (pp_timeout_z(ppi, PP_TO_DELAYREQ)) {
		e = msg_issue_delay_req(ppi);

		ppi->t3 = ppi->last_snt_time;

		/* Restart the timeout for next time */
		pp_timeout_rand(ppi, PP_TO_DELAYREQ,
				DSPOR(ppi)->logMinDelayReqInterval);

		/* Add latency */
		add_TimeInternal(&ppi->t3,
				 &ppi->t3,
				 &OPTS(ppi)->outbound_latency);
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
