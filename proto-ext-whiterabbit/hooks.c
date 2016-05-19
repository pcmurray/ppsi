#include <ppsi/ppsi.h>
#include "wr-api.h"
#include "../proto-standard/common-fun.h"

/* ext-whiterabbit must offer its own hooks */

static int wr_init(struct pp_instance *ppi, void *pkt, int plen)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);

	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	wrp->wrStateTimeout = WR_DEFAULT_STATE_TIMEOUT_MS;
	wrp->calPeriod = WR_DEFAULT_CAL_PERIOD;
	wrp->wrModeOn = 0;
	wrp->parentWrConfig = NON_WR;
	wrp->parentWrModeOn = 0;
	wrp->calibrated = !WR_DEFAULT_PHY_CALIBRATION_REQUIRED;

	if ((wrp->wrConfig & WR_M_AND_S) == WR_M_ONLY)
		wrp->ops->enable_timing_output(ppi, 1);
	else
		wrp->ops->enable_timing_output(ppi, 0);
	return 0;
}

static int wr_open(struct pp_globals *ppg, struct pp_runtime_opts *rt_opts)
{
	int i;

	pp_diag(NULL, ext, 2, "hook: %s\n", __func__);
	/* If current arch (e.g. wrpc) is not using the 'pp_links style'
	 * configuration, just assume there is one ppi instance,
	 * already configured properly by the arch's main loop */
	if (ppg->nlinks == 0) {
		INST(ppg, 0)->ext_data = ppg->global_ext_data;
		return 0;
	}

	for (i = 0; i < ppg->nlinks; i++) {
		struct pp_instance *ppi = INST(ppg, i);

		/* FIXME check if correct: assign to each instance the same
		 * wr_data. May I move it to pp_globals? */
		INST(ppg, i)->ext_data = ppg->global_ext_data;

		if (ppi->cfg.ext == PPSI_EXT_WR) {
			switch (ppi->role) {
				case PPSI_ROLE_MASTER:
					WR_DSPOR(ppi)->wrConfig = WR_M_ONLY;
					break;
				case PPSI_ROLE_SLAVE:
					WR_DSPOR(ppi)->wrConfig = WR_S_ONLY;
					break;
				default:
					WR_DSPOR(ppi)->wrConfig = WR_M_AND_S;
			}
		}
		else
			WR_DSPOR(ppi)->wrConfig = NON_WR;
	}

	return 0;
}

static int wr_listening(struct pp_instance *ppi, void *pkt, int plen)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);

	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	wrp->wrMode = NON_WR;
	return 0;
}

static int wr_handle_preq(struct pp_instance *ppi)
{
	msg_hdr_set_cf(&ppi->received_ptp_header,
		       phase_to_cf_units(ppi->last_rcv_time.phase));
	return 0;
}

static int wr_master_msg(struct pp_instance *ppi, void *pkt, int plen,
			 int msgtype)
{
	struct msg_header_wire *hdr = &ppi->received_ptp_header;
	MsgSignaling wrsig_msg;
	TimeInternal *time = &ppi->last_rcv_time;

	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);

	switch (msgtype) {

	/* This case is modified from the default one */
	case PPM_DELAY_REQ:
		msg_hdr_set_cf(hdr,
			       phase_to_cf_units(ppi->last_rcv_time.phase));
		msg_issue_delay_resp(ppi, time); /* no error check */
		msgtype = PPM_NOTHING_TO_DO;
		break;

	case PPM_PDELAY_REQ:
		wr_handle_preq(ppi);
		msgtype = PPM_NOTHING_TO_DO;
		break;

	/* This is missing in the standard protocol */
	case PPM_SIGNALING:
		msg_unpack_wrsig(ppi, pkt, &wrsig_msg,
				 &(WR_DSPOR(ppi)->msgTmpWrMessageID));
		if ((WR_DSPOR(ppi)->msgTmpWrMessageID == SLAVE_PRESENT) &&
		    (WR_DSPOR(ppi)->wrConfig & WR_M_ONLY)) {
			/* We must start the handshake as a WR master */
			wr_handshake_init(ppi, PPS_MASTER);
		}
		msgtype = PPM_NOTHING_TO_DO;
		break;
	}

	return msgtype;
}

static int wr_new_slave(struct pp_instance *ppi, void *pkt, int plen)
{
	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	wr_servo_init(ppi);
	return 0;
}

static int wr_handle_resp(struct pp_instance *ppi)
{
	struct msg_header_wire *hdr = &ppi->received_ptp_header;
	TimeInternal correction_field;
	TimeInternal *ofm = &DSCUR(ppi)->offsetFromMaster;
	struct wr_dsport *wrp = WR_DSPOR(ppi);

	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);

	/* FIXME: check sub-nano relevance of correction filed */
	cField_to_TimeInternal(&correction_field, msg_hdr_get_cf(hdr));

	/*
	 * If no WR mode is on, run normal code, if T2/T3 are valid.
	 * After we adjusted the pps counter, stamps are invalid, so
	 * we'll have the Unix time instead, marked by "correct"
	 */
	if (!wrp->wrModeOn) {
		if (!ppi->t2.correct || !ppi->t3.correct) {
			pp_diag(ppi, servo, 1,
				"T2 or T3 incorrect, discarding tuple\n");
			return 0;
		}
		pp_servo_got_resp(ppi);
		/*
		 * pps always on if offset less than 1 second,
		 * until ve have a configurable threshold */
		if (ofm->seconds)
			wrp->ops->enable_timing_output(ppi, 0);
		else
			wrp->ops->enable_timing_output(ppi, 1);

	}
	wr_servo_got_delay(ppi, msg_hdr_get_cf(hdr));
	wr_servo_update(ppi);
	return 0;
}

static void wr_s1(struct pp_instance *ppi, struct msg_header_wire *hdr,
		  MsgAnnounce *ann)
{
	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	WR_DSPOR(ppi)->parentIsWRnode =
		((ann->ext_specific & WR_NODE_MODE) != NON_WR);
	WR_DSPOR(ppi)->parentWrModeOn =
		(ann->ext_specific & WR_IS_WR_MODE) ? true : false;
	WR_DSPOR(ppi)->parentCalibrated =
			((ann->ext_specific & WR_IS_CALIBRATED) ? 1 : 0);
	WR_DSPOR(ppi)->parentWrConfig = ann->ext_specific & WR_NODE_MODE;
	DSCUR(ppi)->primarySlavePortNumber =
		DSPOR(ppi)->portIdentity.portNumber;
}

static int wr_execute_slave(struct pp_instance *ppi)
{
	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	if (!WR_DSPOR(ppi)->doRestart)
		return 0;

	ppi->next_state = PPS_INITIALIZING;
	WR_DSPOR(ppi)->doRestart = false;
	return 1; /* the caller returns too */
}

static void wr_handle_announce(struct pp_instance *ppi)
{
	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	if ((WR_DSPOR(ppi)->wrConfig & WR_S_ONLY) &&
	    (1 /* FIXME: Recommended State, see page 33*/) &&
	    (WR_DSPOR(ppi)->parentWrConfig & WR_M_ONLY) &&
	    (!WR_DSPOR(ppi)->wrModeOn || !WR_DSPOR(ppi)->parentWrModeOn)) {
		/* We must start the handshake as a WR slave */
		wr_handshake_init(ppi, PPS_SLAVE);
	}
}

static int wr_handle_followup(struct pp_instance *ppi,
			      TimeInternal *precise_orig_timestamp,
			      TimeInternal *correction_field)
{
	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	if (!WR_DSPOR(ppi)->wrModeOn)
		return 0;

	precise_orig_timestamp->phase = 0;
	wr_servo_got_sync(ppi, precise_orig_timestamp,
			  &ppi->t2);

	if (GLBS(ppi)->delay_mech == PP_P2P_MECH)
		wr_servo_update(ppi);

	return 1; /* the caller returns too */
}

static int wr_handle_presp(struct pp_instance *ppi)
{
	struct msg_header_wire *hdr = &ppi->received_ptp_header;
	TimeInternal correction_field;
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	TimeInternal *ofm = &DSCUR(ppi)->offsetFromMaster;

	/* FIXME: check sub-nano relevance of correction filed */
	cField_to_TimeInternal(&correction_field, msg_hdr_get_cf(hdr));

	/*
	 * If no WR mode is on, run normal code, if T2/T3 are valid.
	 * After we adjusted the pps counter, stamps are invalid, so
	 * we'll have the Unix time instead, marked by "correct"
	 */

	if (!wrp->wrModeOn) {
		if (!ppi->t3.correct || !ppi->t6.correct) {
			pp_diag(ppi, servo, 1,
				"T3 or T6 incorrect, discarding tuple\n");
			return 0;
		}
		pp_servo_got_presp(ppi);
		/*
		 * pps always on if offset less than 1 second,
		 * until ve have a configurable threshold */
		if (ofm->seconds)
			wrp->ops->enable_timing_output(ppi, 0);
		else
			wrp->ops->enable_timing_output(ppi, 1);

		return 0;
	}

	ppi->t4_cf = msg_hdr_get_cf(hdr) & 0xffffffff;
	wr_servo_got_delay(ppi, ppi->t4_cf);
	return 0;
}

static int wr_pack_announce(struct pp_instance *ppi)
{
	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	if (WR_DSPOR(ppi)->wrConfig != NON_WR &&
		WR_DSPOR(ppi)->wrConfig != WR_S_ONLY) {
		msg_pack_announce_wr_tlv(ppi);
		return WR_ANNOUNCE_LENGTH;
	}
	return PP_ANNOUNCE_LENGTH;
}

static void wr_unpack_announce(void *buf, MsgAnnounce *ann)
{
	int msg_len = htons(*(uint16_t *) (buf + 2));

	pp_diag(NULL, ext, 2, "hook: %s\n", __func__);
	if (msg_len > PP_ANNOUNCE_LENGTH)
		msg_unpack_announce_wr_tlv(buf, ann);
}


struct pp_ext_hooks pp_hooks = {
	.init = wr_init,
	.open = wr_open,
	.listening = wr_listening,
	.master_msg = wr_master_msg,
	.new_slave = wr_new_slave,
	.handle_resp = wr_handle_resp,
	.s1 = wr_s1,
	.execute_slave = wr_execute_slave,
	.handle_announce = wr_handle_announce,
	.handle_followup = wr_handle_followup,
	.handle_preq = wr_handle_preq,
	.handle_presp = wr_handle_presp,
	.pack_announce = wr_pack_announce,
	.unpack_announce = wr_unpack_announce,
};
