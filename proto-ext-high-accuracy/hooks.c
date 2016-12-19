#include <ppsi/ppsi.h>
#include <common-fun.h>
#include "ha-api.h"
#include <math.h>

char *ha_l1name[] = {
	[L1SYNC_DISABLED]	= "L1SYNC_DISABLED",
	[L1SYNC_IDLE]		= "L1SYNC_IDLE",
	[L1SYNC_LINK_ALIVE]	= "L1SYNC_LINK_ALIVE",
	[L1SYNC_CONFIG_MATCH]	= "L1SYNC_CONFIG_MATCH",
	[L1SYNC_UP]		= "L1SYNC_UP",
};

/* Disgusting helpers to shorten the conditions */
#define HAS_BITS(what, bits) ((wrp->what & (bits)) == (bits))
#define SET_BITS(what, bits) (wrp->what |= (bits))
#define CLR_BITS(what, bits) (wrp->what &= ~(bits))
#define CONG HA_CONGRUENT
#define TXCO HA_TX_COHERENT
#define RXCO HA_RX_COHERENT

static void ha_print_correction_values(struct pp_instance *ppi)
{
	
	int64_t delayCoefficient;
	delayCoefficient = ppi->asymCorrDS->delayCoefficient.scaledRelativeDifference;
	
	pp_diag(ppi, ext, 2, "ML-correction values in DS (upated):"
		"eL=%d [ps], iL=%d [ps], tpL=%d [ps], dA=%d [ps], aC=%d [alpha<<40] "
		"aC= %lld [alpha<<62]\n",
		scaledNs_to_ps(ppi->tstampCorrDS->egressLatency),
		scaledNs_to_ps(ppi->tstampCorrDS->ingressLatency),
		scaledNs_to_ps(ppi->tstampCorrDS->messageTimestampPointLatency),
		scaledNs_to_ps(ppi->asymCorrDS->delayAsymmetry),
		relativeDiff_to_alpha(ppi->asymCorrDS->delayCoefficient),
		(long long)delayCoefficient);
	
}

/* update DS values of latencies and delay coefficient
 * - these values are provided by HW (i.e. HAL) depending on SFPs, wavelenghts, etc
 * - these values are stored in configurable data sets
 * - the values from data sets are used in calculations
 */
static int ha_update_correction_values(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	uint32_t delta_tx=0;
	uint32_t delta_rx=0;
	int32_t fix_alpha=0;
	int64_t delayCoeff_pos, delayCoeff_neg, delay_Coeff_from_alpha;
	
	pp_diag(ppi, ext, 2, "hook: %s -- ext %i\n", __func__, ppi->cfg.ext);

	/* read the interesting values from HW (i.e. HAL)*/
	if (wrp->ops->read_calib_data(ppi, &delta_tx, &delta_rx,
		&fix_alpha, 0) != WR_HW_CALIB_OK){
		pp_diag(ppi, ext, 2, "hook: %s -- cannot read calib values\n",
			__func__);
		return -1;
	}
	if (wrp->ops->read_delayCoeff(ppi, &delay_Coeff_from_alpha) != WR_HW_CALIB_OK){
		pp_diag(ppi, ext, 2, "hook: %s -- cannot read calib values\n",
			__func__);
		return -1;
	}
	
	ppi->tstampCorrDS->egressLatency  = ps_to_scaledNs((int32_t)delta_tx);
	ppi->tstampCorrDS->ingressLatency  = ps_to_scaledNs((int32_t)delta_rx);
	ppi->tstampCorrDS->messageTimestampPointLatency.scaledNanoseconds = 0; //FIXME: from config
	
	ppi->asymCorrDS->delayAsymmetry.scaledNanoseconds  = 0; //FIXME: from config

	// temporarily fed "by hand"
	// (http://www.ohwr.org/projects/white-rabbit/wiki/Calibration)
	// alpha = 2.6787e-04 = 0.00026787
	// delayCoefficient = alpha * 2^62 = 1.235332334*10^15 = 1235332334000000
	// alpha oposite: alpha_oposite = [1/(1+alpha)]-1
	//delayCoeff_by_hand_pos =  1235332333756144;
	//delayCoeff_by_hand_neg = -1235001214700021;//calculated from negative alpha
	//                         -1235001513901056;//this is precise value from matlab
	delayCoeff_pos = 1235332333756144;
	delayCoeff_neg = -((delayCoeff_pos)/(((int64_t)1<<62)+delayCoeff_pos)) << 62;
	
	pp_diag(ppi, ext, 2, "ML-correction values read from HAL :"
		"eL=%d [ps], iL=%d [ps], tpL=%d [ps], dA=%d [ps]\n",
		delta_tx, delta_rx, 0, 0);
	pp_diag(ppi, ext, 2, "ML-delayCoeff hardcoded                (pos): = %lld\n",
		(long long)delayCoeff_pos);
	pp_diag(ppi, ext, 2, "ML-delayCoeff calculated from positive (neg): = %lld\n",
		(long long)delayCoeff_neg);
	pp_diag(ppi, ext, 2, "ML-delayCoeff calculated from alpha   : = %lld\n",
		(long long)delay_Coeff_from_alpha);
	pp_diag(ppi, ext, 2, "ML-fixed alpha                        : = %d\n",
		fix_alpha);
// 	ppi->asymCorrDS->delayCoefficient.scaledRelativeDifference = delay_Coeff_from_alpha;
	if(fix_alpha < 0)
		ppi->asymCorrDS->delayCoefficient.scaledRelativeDifference = delayCoeff_pos;
	else
		ppi->asymCorrDS->delayCoefficient.scaledRelativeDifference = delayCoeff_pos;
	
	ha_print_correction_values(ppi);
	return 0;
}


/* open is global; called from "pp_init_globals" */
static int ha_open(struct pp_globals *ppg, struct pp_runtime_opts *rt_opts)
{
	pp_diag(NULL, ext, 2, "hook: %s -- ext %i\n", __func__,
		INST(ppg,0)->cfg.ext);
	return 0;
}

/* initialize one specific port */
static int ha_init(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);

	pp_diag(ppi, ext, 2, "hook: %s -- ext %i\n", __func__,
		ppi->cfg.ext);

	/* ext_data is the wr-servo */
	ppi->ext_data = ppi->glbs->global_ext_data;


#ifdef CONFIG_ARCH_WRPC
	/* HACK: we can't get the value here (but WR manages to) */
	if (ppi->cfg.ext != PPSI_EXT_HA) {
		pp_diag(ppi, ext, 2, "wrpc: force HA on\n");
		ppi->cfg.ext = PPSI_EXT_HA;
	}
#endif


	wrp->logL1SyncInterval = 0;	/* Once per second. Average? */
	wrp->L1SyncInterval = 4 << (wrp->logL1SyncInterval + 8);
	wrp->L1SyncReceiptTimeout = HA_DEFAULT_L1SYNCRECEIPTTIMEOUT;

	/*
	 * It looks like we can't check cfg.ext at init time
	 * because the value can change at run time (pity me). So
	 * go disabled, which will then clear all bits at 1st iteration
	 */
	wrp->L1SyncState = L1SYNC_DISABLED; /* Changed later */

	/*
	 * It looks like the braindead specification wants these to eb
	 * changed by management, but never by the state machine. So
	 * even if disabled I have this configuration, that for us is
	 * always 111 (clearly tx and rx are undistinguishable or nothing
	 * will work in the implementation (we compare tx with peer-tx)
	 */
	SET_BITS(ha_conf, CONG | TXCO | RXCO);
	return 0;
}

/* This hook is called whenever a signaling message is received */
static int ha_handle_signaling(struct pp_instance * ppi,
			       unsigned char *pkt, int plen)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	int to_rx;

	pp_diag(ppi, ext, 2, "hook: %s (%i:%s) -- plen %i\n", __func__,
		ppi->state, ha_l1name[wrp->L1SyncState], plen);

	to_rx = wrp->L1SyncInterval * wrp->L1SyncReceiptTimeout;
	__pp_timeout_set(ppi, HA_TO_RX, to_rx);

	wrp->ha_link_ok = 1;

	ha_unpack_signal(ppi, pkt, plen);
	return 0;
}

/*
 * This hook is called by most states (master/slave etc) to do stuff
 * and send signalling messages; it returns the ext-specific timeout value
 */
static int ha_calc_timeout(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	int to_tx, do_xmit = 0;
	int config_ok, state_ok;

	pp_diag(ppi, ext, 2, "hook: %s (%i:%s)\n", __func__,
		ppi->state, ha_l1name[wrp->L1SyncState]);

	to_tx = wrp->L1SyncInterval; /* randomize? */

	if (ppi->is_new_state) { /* sth changed (or beginning of the world) */
		do_xmit = 1;
		/*
		 * There is one special case: if we are a _new_ master,
		 * we clear RX_COHERENT. And unlock the pll just to be sure.
		 */
		if (ppi->state == PPS_MASTER) {
			CLR_BITS(ha_active, RXCO);
			wrp->ops->locking_disable(ppi);
		}
	}

	/* If TX time expired, transmit */
	if (pp_timeout(ppi, HA_TO_TX))
		do_xmit = 1;

	/* If we stopped receiving go back */
	if (wrp->ha_link_ok && pp_timeout(ppi, HA_TO_RX)) {
		wrp->ha_link_ok = 0;
		wrp->L1SyncState = L1SYNC_IDLE;
		wrp->ha_peer_conf = wrp->ha_peer_active = 0;
		if (ppi->state == PPS_SLAVE)
			wrp->ops->locking_disable(ppi);
		do_xmit = 1;
	}
	/* From any state, if now disabled, disable */
	if (ppi->cfg.ext != PPSI_EXT_HA)
		wrp->L1SyncState = L1SYNC_DISABLED;

	/* Always check the pll status, whether or not we turned it on */
	if (ppi->state == PPS_SLAVE  && HAS_BITS(ha_conf, CONG)) {
		if (wrp->ops->locking_poll(ppi, 0) == WR_SPLL_READY) {
			/* we could just set, but detect edge to report it */
			if (!HAS_BITS(ha_active, RXCO)) {
				pp_diag(ppi, ext, 1, "PLL is locked\n");
				do_xmit = 1;
			}
			SET_BITS(ha_active, RXCO);
		} else {
			CLR_BITS(ha_active, RXCO);
		}
	}
	/* Similarly, always upgrade master's active according to the slave */
	if (ppi->state == PPS_MASTER && HAS_BITS(ha_conf, CONG)) {
		if (HAS_BITS(ha_peer_active, RXCO | TXCO)
		    &&
		    HAS_BITS(ha_active, TXCO)) {
			/* peer is both and I am tx -- so I am rx too */
			SET_BITS(ha_active, RXCO);
		} else {
			CLR_BITS(ha_active, RXCO);
		}
	}

	/*
	 * L1 state machine; calculate what trigger state changes
	 */
	config_ok = (wrp->ha_conf == wrp->ha_peer_conf); /* conf must match */

	/* For state, if bits in conf are 0, we don't care about bits in act */
	state_ok = (wrp->ha_conf & wrp->ha_active & wrp->ha_peer_active)
		== wrp->ha_conf; /* if in conf, it must be in both act */

	switch(wrp->L1SyncState) {

	case L1SYNC_DISABLED: /* Likely beginning of the world, or new cfg */
		wrp->wrModeOn = 0;
		wrp->ha_active = 0;
		do_xmit = 0;
		if (ppi->cfg.ext != PPSI_EXT_HA)
			break;

		wrp->L1SyncState = L1SYNC_IDLE;
		do_xmit = 1;
		/* and fall through */

	case L1SYNC_IDLE: /* If verified to be direct and active... */
		wrp->wrModeOn = 0;
		if (!wrp->ha_link_ok)
			break;

		wrp->L1SyncState = L1SYNC_LINK_ALIVE;
		SET_BITS(ha_active, CONG | TXCO);
		/* and fall through */

	case L1SYNC_LINK_ALIVE: /* Check cfg: start locking if ok */
		if (!config_ok)
			break;
		if (ppi->state != PPS_SLAVE && ppi->state != PPS_MASTER)
			break; /* remain in alive, don't proceed */

		/* configuration matches, so the slave must lock */
		if (ppi->state == PPS_SLAVE && HAS_BITS(ha_conf, CONG)) {
			pp_diag(ppi, ext, 1, "Locking PLL\n");
			wrp->ops->locking_enable(ppi);
		}

		/* master or slave: proceed to match */
		wrp->L1SyncState = L1SYNC_CONFIG_MATCH;

	case L1SYNC_CONFIG_MATCH:
		if (!config_ok) { /* config_ok set above */
			wrp->L1SyncState = L1SYNC_LINK_ALIVE;
			break;
		}
		if (!state_ok) /* state_ok set above, after polling */
			break;

		wrp->L1SyncState = L1SYNC_UP;
		ha_update_correction_values(ppi);
		// Do what was done in WR LINK ON: 
		wrp->wrModeOn = TRUE;
		wrp->parentWrModeOn = TRUE;
		wrp->ops->enable_ptracker(ppi);
		/* and fall through */

	case L1SYNC_UP:
		/* FIXME: manage tracking-lost event, if so   state_ok := 0 */

		if (!state_ok)
			/* FIXME: go back to config_match, and pll? */;
		if (!config_ok)
			/* FIXME: go back to link_alive, and pll? */;

		
		break;
	}

	if (do_xmit) {/* transmitting is simple */
		int len;

		pp_diag(ppi, ext, 1, "Sending signaling msg\n");
		len = ha_pack_signal(ppi);
		/* FIXME: check the destination MAC address */
		__send_and_log(ppi, len, PP_NP_GEN);

		__pp_timeout_set(ppi, HA_TO_TX, to_tx); /* loop ever since */
	}

	/* Return the timeout for next invocation */
	return pp_next_delay_1(ppi, HA_TO_TX);
}

/*
 * Following functions are mainly copied from proto-ext-wr, with small changes
 * We are *not* using most wrp-> fields. See ha-api.h for a check
 */
static int wr_handle_preq(struct pp_instance *ppi)
{
	ppi->received_ptp_header.correctionfield.msb = 0;
	ppi->received_ptp_header.correctionfield.lsb =
		phase_to_cf_units(ppi->last_rcv_time.phase);
	return 0;
}

static int ha_master_msg(struct pp_instance *ppi, unsigned char *pkt, int plen,
			 int msgtype)
{
	MsgHeader *hdr = &ppi->received_ptp_header;
	TimeInternal *time = &ppi->last_rcv_time;

	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);

	switch (msgtype) {

	/* This case is modified from the default one */
	case PPM_DELAY_REQ:
		hdr->correctionfield.msb = 0;
		hdr->correctionfield.lsb =
			phase_to_cf_units(ppi->last_rcv_time.phase);
		msg_issue_delay_resp(ppi, time); /* no error check */
		msgtype = PPM_NOTHING_TO_DO;
		break;

	case PPM_PDELAY_REQ:
		wr_handle_preq(ppi);
		msgtype = PPM_NOTHING_TO_DO;
		break;

		/* No signaling stuff copied over from WR */
	}

	return msgtype;
}

static int ha_new_slave(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	wr_servo_init(ppi);
	return 0;
}

static int ha_handle_resp(struct pp_instance *ppi)
{
	MsgHeader *hdr = &ppi->received_ptp_header;
	TimeInternal correction_field;
	TimeInternal *ofm = &DSCUR(ppi)->offsetFromMaster;
	struct wr_dsport *wrp = WR_DSPOR(ppi);

	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);

	/* FIXME: check sub-nano relevance of correction filed */
	cField_to_TimeInternal(&correction_field, hdr->correctionfield);

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
	wr_servo_got_delay(ppi, hdr->correctionfield.lsb);
	wr_servo_update(ppi);
	return 0;
}

/* Hmm... "execute_slave" should look for errors; but it's off in WR too */
static int ha_handle_followup(struct pp_instance *ppi,
			      TimeInternal *precise_orig_timestamp,
			      TimeInternal *correction_field)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);

	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	if (wrp->L1SyncState != L1SYNC_UP)
		return 0;

	precise_orig_timestamp->phase = 0;
	wr_servo_got_sync(ppi, precise_orig_timestamp,
			  &ppi->t2);

	if (ppi->mech == PP_P2P_MECH)
		wr_servo_update(ppi);

	return 1; /* the caller returns too */
}

static int ha_handle_presp(struct pp_instance *ppi)
{
	MsgHeader *hdr = &ppi->received_ptp_header;
	TimeInternal correction_field;
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	TimeInternal *ofm = &DSCUR(ppi)->offsetFromMaster;

	/* FIXME: check sub-nano relevance of correction filed */
	cField_to_TimeInternal(&correction_field, hdr->correctionfield);

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

	ppi->t4_cf = hdr->correctionfield.lsb;
	wr_servo_got_delay(ppi, ppi->t4_cf);
	return 0;
}

static int ha_handle_preq(struct pp_instance *ppi)
{
	ppi->received_ptp_header.correctionfield.msb = 0;
	ppi->received_ptp_header.correctionfield.lsb =
		phase_to_cf_units(ppi->last_rcv_time.phase);
	return 0;
}

/* The global structure used by ppsi */
struct pp_ext_hooks pp_hooks = {
	.open = ha_open, /* misnamed: global init  */
	.init = ha_init, /* misnamed: port open */
	.handle_signaling = ha_handle_signaling,
	.calc_timeout = ha_calc_timeout,

	/*
	 * All the rest is wr-derived, to run the WR servo
	 */
	.master_msg = ha_master_msg,
	.new_slave = ha_new_slave,
	.handle_resp = ha_handle_resp,
	.handle_followup = ha_handle_followup,
	.handle_preq = ha_handle_preq,
	.handle_presp = ha_handle_presp,
};
