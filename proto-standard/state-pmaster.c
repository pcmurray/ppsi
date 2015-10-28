/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_pmaster(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	TimeInternal *time_snt;
	int msgtype, d1, d2;
	int e = 0; /* error var, to check errors in msg handling */
	
	struct wr_dsport *wrp_hsr0 = WR_DSPOR(INST(ppi->glbs, 0));
	struct wr_dsport *wrp_hsr1 = WR_DSPOR(INST(ppi->glbs, 1));

	if(wrp_hsr0->wrModeOn || wrp_hsr1->wrModeOn){
		ppi->master_only = 0;
		ppi->slave_only = 0;
		ppi->backup_only = 1;
		ppi->slave_prio = 0;
		ppi->next_state = PPS_SLAVE;
		return 0;
	}

	if (ppi->is_new_state) {
		pp_timeout_rand(ppi, PP_TO_SYNC, DSPOR(ppi)->logSyncInterval);
		pp_timeout_rand(ppi, PP_TO_ANN_INTERVAL,
				DSPOR(ppi)->logAnnounceInterval);

#ifdef CONFIG_E2E
		/* Send an announce immediately, when becomes master */
		if ((e = msg_issue_announce(ppi)) < 0)
			goto out;
#endif
	}

	if (pp_timeout_z(ppi, PP_TO_SYNC)) {
#ifdef CONFIG_E2E
		if ((e = msg_issue_sync(ppi) < 0))
			goto out;

		time_snt = &ppi->last_snt_time;
		add_TimeInternal(time_snt, time_snt,
				 &OPTS(ppi)->outbound_latency);
		if ((e = msg_issue_followup(ppi, time_snt)))
			goto out;
#endif
		/* Restart the timeout for next time */
		pp_timeout_rand(ppi, PP_TO_SYNC, DSPOR(ppi)->logSyncInterval);
	}

	if (pp_timeout_z(ppi, PP_TO_ANN_INTERVAL)) {
#ifdef CONFIG_E2E
		if ((e = msg_issue_announce(ppi) < 0))
			goto out;
#endif

		/* Restart the timeout for next time */
		pp_timeout_rand(ppi, PP_TO_ANN_INTERVAL,
				DSPOR(ppi)->logAnnounceInterval);
	}

	/* keep it for later --> hsr ring round trip */
	/*if (ppi->is_new_state) {
		pp_servo_init(ppi);

		if (pp_hooks.new_slave)
			e = pp_hooks.new_slave(ppi, pkt, plen);
		if (e)
			goto out;

		ppi->waiting_for_follow = FALSE;
		ppi->waiting_for_resp_follow = FALSE;

		pp_timeout_restart_annrec(ppi);
		pp_timeout_rand(ppi, PP_TO_PDELAYREQ,
				DSPOR(ppi)->logMinPDelayReqInterval);
	}*/

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
#ifdef CONFIG_P2P
		tc_forward_ann(ppi, pkt, plen); /* P2P - Transp. Clocks */
#endif
		break;

	case PPM_SYNC:
#ifdef CONFIG_P2P
		tc_forward_sync(ppi, pkt, plen); /* P2P - Transp. Clocks */
#endif
		break;

	case PPM_FOLLOW_UP:
#ifdef CONFIG_P2P
		tc_forward_followup(ppi, pkt, plen); /* P2P - Transp. Clocks */
#endif
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
	if (pp_timeout_z(ppi, PP_TO_PDELAYREQ)) {
		e = msg_issue_pdelay_req(ppi);
		ppi->t3 = ppi->last_snt_time;

		/* Restart the timeout for next time */
		pp_timeout_rand(ppi, PP_TO_PDELAYREQ,
				DSPOR(ppi)->logMinPDelayReqInterval);
	}
	
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
