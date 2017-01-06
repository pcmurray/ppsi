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

void ha_print_correction_values(struct pp_instance *ppi)
{
	
	int64_t delayCoefficient;
	delayCoefficient = ppi->asymCorrDS->delayCoefficient.scaledRelativeDifference;
	
	pp_diag(ppi, ext, 2, "ML-correction values in DS (upated):"
		"eL=%d [ps], iL=%d [ps], tpL=%d [ps], dA=%d [ps] "
		"aC= %lld, alpha*e10 = %lld \n",
		scaledNs_to_ps(ppi->tstampCorrDS->egressLatency),
		scaledNs_to_ps(ppi->tstampCorrDS->ingressLatency),
		scaledNs_to_ps(ppi->tstampCorrDS->messageTimestampPointLatency),
		scaledNs_to_ps(ppi->asymCorrDS->delayAsymmetry),
		(long long)delayCoefficient,
		(long long)(relativeDiff_to_alpha(ppi->asymCorrDS->delayCoefficient)*(int64_t)10000000000));
	
}
void ha_print_L1Sync_basic_bitmaps(struct pp_instance *ppi, uint8_t configed,
					uint8_t active, char* text)
{
	
	pp_diag(ppi, ext, 2, "ML: L1Sync %s\n", text);
	pp_diag(ppi, ext, 2, "ML: \tConfig: TxC=%d RxC=%d Cong=%d Param=%d\n",
		  ((configed & HA_TX_COHERENT) == HA_TX_COHERENT),
		  ((configed & HA_RX_COHERENT) == HA_RX_COHERENT),
		  ((configed & HA_CONGRUENT)   == HA_CONGRUENT),
		  ((configed & HA_OPT_PARAMS)  == HA_OPT_PARAMS));
	pp_diag(ppi, ext, 2, "ML: \tActive: TxC=%d RxC=%d Cong=%d\n",
		  ((active & HA_TX_COHERENT)   == HA_TX_COHERENT),
		  ((active & HA_RX_COHERENT)   == HA_RX_COHERENT),
		  ((active & HA_CONGRUENT)     == HA_CONGRUENT));
}
/* update DS values of latencies and delay coefficient
 * - these values are provided by HW (i.e. HAL) depending on SFPs, wavelenghts, etc
 * - these values are stored in configurable data sets
 * - the values from data sets are used in calculations
 */
int ha_update_correction_values(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	struct wr_servo_state *s =
			&((struct wr_data *)ppi->ext_data)->servo_state;
	int64_t fix_alpha=0;
	
	pp_diag(ppi, ext, 2, "hook: %s -- ext %i\n", __func__, ppi->cfg.ext);


	/* read the interesting values from HW (i.e. HAL)*/
	if (wrp->ops->read_corr_data(ppi, 
		&ppi->asymCorrDS->delayCoefficient.scaledRelativeDifference,
		&ppi->tstampCorrDS->ingressLatency.scaledNanoseconds,
		&ppi->tstampCorrDS->egressLatency.scaledNanoseconds,
		&ppi->tstampCorrDS->messageTimestampPointLatency.scaledNanoseconds,
		&ppi->asymCorrDS->delayAsymmetry.scaledNanoseconds,
		&fix_alpha,
		&s->clock_period_ps) != WR_HW_CALIB_OK){
		      pp_diag(ppi, ext, 2, "hook: %s -- cannot read calib values\n",
			__func__);
		return -1;
	}
	s->fiber_fix_alpha = (int32_t)fix_alpha; //TODO: change alpha in servo struct
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
	// init configurable data set members with proper confg values
	ppi->L1BasicDS->L1SyncEnabled            = 1;
	ppi->L1BasicDS->txCoherentConfigured     = 1;
	ppi->L1BasicDS->rxCoherentConfigured     = 1;
	ppi->L1BasicDS->congruentConfigured      = 1;
	ppi->L1BasicDS->optParamsEnabled         = 0;
	ppi->L1BasicDS->logL1SyncInterval        = 0;
	ppi->L1BasicDS->L1SyncReceiptTimeout     = HA_DEFAULT_L1SYNCRECEIPTTIMEOUT;
	// init dynamic data set members with zeros/defaults
	ppi->L1BasicDS->L1SyncLinkAlive          = 0;
	ppi->L1BasicDS->txCoherentActive         = 0;
	ppi->L1BasicDS->rxCoherentActive         = 0;
	ppi->L1BasicDS->congruentActive          = 0;
	ppi->L1BasicDS->L1SyncState              = L1SYNC_DISABLED;
	ppi->L1BasicDS->peerTxCoherentConfigured = 0;
	ppi->L1BasicDS->peerRxCoherentConfigured = 0;
	ppi->L1BasicDS->peerCongruentConfigured  = 0;
	ppi->L1BasicDS->peerTxCoherentActive     = 0;
	ppi->L1BasicDS->peerRxCoherentActive     = 0;
	ppi->L1BasicDS->peerCongruentActive      = 0;
	
// 	wrp->logL1SyncInterval                   = 0;/* Once per second. Average? */
// 	wrp->L1SyncInterval = 4 << (wrp->logL1SyncInterval + 8);
// 	wrp->L1SyncReceiptTimeout = HA_DEFAULT_L1SYNCRECEIPTTIMEOUT;

	return 0;
}

/* This hook is called whenever a signaling message is received */
static int ha_handle_signaling(struct pp_instance * ppi,
			       unsigned char *pkt, int plen)
{
	int to_rx;

	pp_diag(ppi, ext, 2, "hook: %s (%i:%s) -- plen %i\n", __func__,
		ppi->state, ha_l1name[ppi->L1BasicDS->L1SyncState], plen);
	
	to_rx = (4 << (ppi->L1BasicDS->logL1SyncInterval + 8)) *
		ppi->L1BasicDS->L1SyncReceiptTimeout;
	__pp_timeout_set(ppi, HA_TO_RX, to_rx);

	ppi->L1BasicDS->L1SyncLinkAlive = 1;

	ha_unpack_signal(ppi, pkt, plen);
	return 0;
}

uint8_t ha_L1Sync_creat_bitmask(int tx_coh, int rx_coh, int congru)
{
	uint8_t outputMask=0;
	if(tx_coh) outputMask |= HA_TX_COHERENT;
	if(rx_coh) outputMask |= HA_RX_COHERENT;
	if(congru) outputMask |= HA_CONGRUENT;
	return outputMask;
}
/*
 * This hook is called by most states (master/slave etc) to do stuff
 * and send signalling messages; it returns the ext-specific timeout value
 */
static int ha_calc_timeout(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	int to_tx, do_xmit = 0;
	int config_ok, state_ok, link_ok, l1_sync_enabled, l1_sync_reset;
	uint8_t local_config, peer_config, local_active, peer_active;

	pp_diag(ppi, ext, 2, "ML: enter L1Sync with state %s and LinkActive = %d \n",
		  ha_l1name[ppi->L1BasicDS->L1SyncState], ppi->L1BasicDS->L1SyncLinkAlive);

	/* L1 needs to follow PTP FSM when congruent */
	if (ppi->is_new_state && ppi->L1BasicDS->congruentConfigured == 1) {
		do_xmit = 1;
		/*
		 * There is one special case: if we are a _new_ master,
		 * we clear RX_COHERENT. And unlock the pll just to be sure.
		 */
		if (ppi->state == PPS_MASTER) {
			ppi->L1BasicDS->rxCoherentActive = 0;
			wrp->ops->locking_disable(ppi);
		}
	}
	
	/** *************** Update of dynamic data set members ****************/
	/* Check whether signaling messages are still recevied*/
	/* If we stopped receiving go back */
	if (ppi->L1BasicDS->L1SyncLinkAlive && pp_timeout(ppi, HA_TO_RX)) {
		ppi->L1BasicDS->L1SyncLinkAlive = 0;
		if (ppi->state == PPS_SLAVE)
			wrp->ops->locking_disable(ppi);
		do_xmit = 1;
	}

	/* Always check the pll status, whether or not we turned it on */
	if (ppi->state == PPS_SLAVE  && ppi->L1BasicDS->congruentConfigured == 1) {
		if (wrp->ops->locking_poll(ppi, 0) == WR_SPLL_READY) {
			/* we could just set, but detect edge to report it */
			if (ppi->L1BasicDS->rxCoherentActive == 0) {
				pp_diag(ppi, ext, 1, "PLL is locked\n");
				do_xmit = 1; //announce as soon as locked, permitted by std
			}
			ppi->L1BasicDS->rxCoherentActive = 1;
		} else {
			ppi->L1BasicDS->rxCoherentActive = 0;
		}
	}
	/* Similarly, always upgrade master's active according to the slave */
	if (ppi->state == PPS_MASTER && ppi->L1BasicDS->congruentConfigured == 1) {
		if (ppi->L1BasicDS->peerTxCoherentActive == 1 &&
		    ppi->L1BasicDS->peerRxCoherentActive == 1 &&
		    ppi->L1BasicDS->txCoherentActive     == 1) {
			/* peer is both and I am tx -- so I am rx too */
			ppi->L1BasicDS->rxCoherentActive = 1;
		} else {
			ppi->L1BasicDS->rxCoherentActive = 0;
		}
	}
	/* By design, when link is up, it is always tx coherent and congruent*/
	if (WR_DSPOR(ppi)->linkUP){
		ppi->L1BasicDS->txCoherentActive = 1;
		ppi->L1BasicDS->congruentActive  = 1;
	}
	else {
		ppi->L1BasicDS->txCoherentActive = 0;
		ppi->L1BasicDS->congruentActive  = 0;
	}
	
	if(ppi->L1BasicDS->L1SyncState == L1SYNC_DISABLED ||
		ppi->L1BasicDS->L1SyncState == L1SYNC_IDLE){
		ppi->L1BasicDS->peerTxCoherentActive     = 0;
		ppi->L1BasicDS->peerRxCoherentActive     = 0;
		ppi->L1BasicDS->peerCongruentActive      = 0;
	}
	/** ***************** create bit masks (to make things easier)******************/
	// create bit masks
	local_config = ha_L1Sync_creat_bitmask(ppi->L1BasicDS->txCoherentConfigured,
	                                       ppi->L1BasicDS->rxCoherentConfigured,
	                                       ppi->L1BasicDS->congruentConfigured);
	peer_config  = ha_L1Sync_creat_bitmask(ppi->L1BasicDS->peerTxCoherentConfigured,
	                                       ppi->L1BasicDS->peerRxCoherentConfigured,
	                                       ppi->L1BasicDS->peerCongruentConfigured);
	local_active = ha_L1Sync_creat_bitmask(ppi->L1BasicDS->txCoherentActive,
	                                       ppi->L1BasicDS->rxCoherentActive,
	                                       ppi->L1BasicDS->congruentActive);
	peer_active  = ha_L1Sync_creat_bitmask(ppi->L1BasicDS->peerTxCoherentActive,
	                                       ppi->L1BasicDS->peerRxCoherentActive,
	                                       ppi->L1BasicDS->peerCongruentActive);
	
	ha_print_L1Sync_basic_bitmaps(ppi, local_config,local_active, "Local (ex optParam)");
	ha_print_L1Sync_basic_bitmaps(ppi, local_config,local_active, "Peer  (ex optParam)");
	
	/** ******** state transition variables (table 140) ****************/
	/*
	 * L1 state machine; calculate what trigger state changes
	 */
	l1_sync_enabled = (ppi->cfg.ext == PPSI_EXT_HA) &&
				  (ppi->L1BasicDS->L1SyncEnabled == 1);
	link_ok         = (ppi->L1BasicDS->L1SyncLinkAlive == 1);
	config_ok       = (local_config == peer_config); /* conf must match */
	state_ok        = (local_config & local_active & peer_active)
				  == local_config; /* if in conf, it must be in both act */
	l1_sync_reset   = (WR_DSPOR(ppi)->linkUP == 0);

	/** ***************** state machine (Figure 62)******************/
	/* transmissions from any state*/
	if (!l1_sync_enabled || l1_sync_reset)
		ppi->L1BasicDS->L1SyncState = L1SYNC_DISABLED;

	if (!link_ok)
		ppi->L1BasicDS->L1SyncState = L1SYNC_IDLE;
	
	/*state machine*/
	switch(ppi->L1BasicDS->L1SyncState) {

	case L1SYNC_DISABLED: /* Likely beginning of the world, or new cfg */
		//TODO: remove this dependency on WR
		wrp->wrModeOn       = 0;
		wrp->parentWrModeOn = 0;
		do_xmit = 0;
		if (l1_sync_enabled==0)
			break;

		ppi->L1BasicDS->L1SyncState = L1SYNC_IDLE;
		do_xmit = 1;
		/* and fall through */

	case L1SYNC_IDLE: /* If verified to be direct and active... */
		//TODO: remove this dependency on WR
		wrp->wrModeOn       = 0;
		wrp->parentWrModeOn = 0;
		if (link_ok==0)
			break;

		ppi->L1BasicDS->L1SyncState      = L1SYNC_LINK_ALIVE;
		/* and fall through */

	case L1SYNC_LINK_ALIVE: /* Check cfg: start locking if ok */
		//TODO: remove this dependency on WR
		wrp->wrModeOn       = 0;
		wrp->parentWrModeOn = 0;
		if (!config_ok)
			break;

		/* master or slave: proceed to match */
		ppi->L1BasicDS->L1SyncState= L1SYNC_CONFIG_MATCH;

	case L1SYNC_CONFIG_MATCH:
		//TODO: remove this dependency on WR
		wrp->wrModeOn       = 0;
		wrp->parentWrModeOn = 0;
		if (!config_ok) { /* config_ok set above */
			ppi->L1BasicDS->L1SyncState = L1SYNC_LINK_ALIVE;
			break;
		}
		
		/* apply the config, which means lock on slave, do nothing on master */
		if (ppi->state == PPS_SLAVE && ppi->L1BasicDS->congruentConfigured == 1 &&
			ppi->L1BasicDS->rxCoherentActive==0) {
			pp_diag(ppi, ext, 1, "Locking PLL\n");
			wrp->ops->locking_enable(ppi);
		}
		if (ppi->state == PPS_MASTER && ppi->L1BasicDS->congruentConfigured == 1) {
			wrp->ops->locking_disable(ppi);
		}
		
		if (!state_ok) /* state_ok set above, after polling */
			break;

		ppi->L1BasicDS->L1SyncState = L1SYNC_UP;
		//TODO: remove this dependency on WR
		wrp->wrModeOn       = 1;
		wrp->parentWrModeOn = 1;
		ha_update_correction_values(ppi);
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

	/** ***************** Transmit L1_SYNC_TLV******************/
	/* If neededy by implemenation above or when TX time expired, transmit */
	if (pp_timeout(ppi, HA_TO_TX))
		do_xmit = 1;
	
	if (ppi->L1BasicDS->L1SyncState != L1SYNC_DISABLED && do_xmit == 1) {/* transmitting is simple */
		int len;

		pp_diag(ppi, ext, 1, "Sending signaling msg\n");
		len = ha_pack_signal(ppi);
		/* FIXME: check the destination MAC address */
		__send_and_log(ppi, len, PP_NP_GEN);
		
		to_tx = 4 << (ppi->L1BasicDS->logL1SyncInterval + 8);
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
	pp_diag(ppi, ext, 2, "hook: %s\n", __func__);
	if (ppi->L1BasicDS->L1SyncState  != L1SYNC_UP)
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
