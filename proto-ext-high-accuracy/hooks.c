#include <ppsi/ppsi.h>
#include <common-fun.h>
#include "ha-api.h"

char *ha_l1name[] = {
	[L1SYNC_DISABLED]	= "L1SYNC_DISABLED",
	[L1SYNC_IDLE]		= "L1SYNC_IDLE",
	[L1SYNC_LINK_ALIVE]	= "L1SYNC_LINK_ALIVE",
	[L1SYNC_CONFIG_MATCH]	= "L1SYNC_CONFIG_MATCH",
	[L1SYNC_UP]		= "L1SYNC_UP",
};

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


#ifdef CONFIG_ARCH_WRPC
	/* HACK: we can't get the value here (but WR manages to) */
	if (ppi->cfg.ext != PPSI_EXT_HA) {
		pp_diag(ppi, ext, 2, "wrpc: force HA on\n");
		ppi->cfg.ext = PPSI_EXT_HA;
	}
#endif

	/* If no extension is configured, nothing to do */
	if (ppi->cfg.ext != PPSI_EXT_HA) {
		pp_diag(ppi, ext, 2, "HA not enabled at config time\n");
		wrp->L1SyncState = L1SYNC_DISABLED;
		return 0;
	}

	/*
	 * Otherwise, set up the minimal bits, and be ready to start
	 */

	wrp->ha_conf = HA_TX_COHERENT | HA_CONGRUENT; /* always */
	wrp->ha_active = HA_TX_COHERENT;

	wrp->logL1SyncInterval = 0;	/* Once per second. Average? */
	wrp->L1SyncInterval = 4 << (wrp->logL1SyncInterval + 8);
	wrp->L1SyncReceiptTimeout = HA_DEFAULT_L1SYNCRECEIPTTIMEOUT;
	wrp->L1SyncState = L1SYNC_IDLE; /* Enabled but nothing being done */
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
	wrp->rx_l1_count++;
	if (wrp->rx_l1_count <= 0) /* overflow? */
		wrp->rx_l1_count = 1;

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

	pp_diag(ppi, ext, 2, "hook: %s (%i:%s)\n", __func__,
		ppi->state, ha_l1name[wrp->L1SyncState]);

	to_tx = wrp->L1SyncInterval; /* randomize? */

	/*
	 * Track changes in ptp state machine. Also this ensures
	 * we initialize the timeout at the first run (is_new_state == 1)
	 */
	if (ppi->is_new_state) {
		do_xmit = 1;

		switch(ppi->state) {
		case PPS_LISTENING: /* master/slave unknown: disable */
			pp_diag(ppi, ext, 1, "PTP listening -> L1 idle");
			wrp->ha_conf &= ~HA_RX_COHERENT;
			wrp->ha_active &= ~HA_RX_COHERENT;
			wrp->ops->locking_disable(ppi);
			wrp->ops->enable_timing_output(ppi, 0);
			wrp->L1SyncState = L1SYNC_IDLE;
			wrp->rx_l1_count = 0;
			wrp->wrModeOn = 0;
			break;

		case PPS_MASTER:
			pp_diag(ppi, ext, 1, "PTP master -> L1 signals\n");
			wrp->ha_conf &= ~HA_RX_COHERENT;
			break;

		case PPS_SLAVE:
			pp_diag(ppi, ext, 1, "PTP slave -> L1 signals\n");
			wrp->ha_conf |= HA_RX_COHERENT;
			break;
		}
	}

	/* If not new state, but on time, send your signaling message */
	if (!do_xmit && pp_timeout(ppi, HA_TO_TX))
		do_xmit = 1;

	/* If we already received something, but no more, honor the timeout */
	if (wrp->rx_l1_count && pp_timeout(ppi, HA_TO_RX)) {
		wrp->rx_l1_count = 0;
		wrp->L1SyncState = L1SYNC_IDLE;
		wrp->ha_active &= ~HA_RX_COHERENT; /* unlock pll? */
		wrp->ha_active &= ~HA_CONGRUENT; /* unlock pll? */
		do_xmit = 1;
	}

	/* L1 state machine; according to what we know about the peer */
	switch(wrp->L1SyncState) {

	case L1SYNC_DISABLED: /* Nothing to do */
		break;

	case L1SYNC_IDLE: /* If verified to be direct and active... */
		wrp->wrModeOn = 0;
		if (wrp->rx_l1_count) {
			wrp->L1SyncState = L1SYNC_LINK_ALIVE;
			/* and fall through */
		} else {
			break;
		}

	case L1SYNC_LINK_ALIVE: /* Check cfg: start locking if ok */
		if ((wrp->ha_conf & HA_RX_COHERENT) &&
		    !(wrp->ha_peer_conf & HA_RX_COHERENT)) {
			/* We are slave, with a master on the link. Lock */
			wrp->L1SyncState = L1SYNC_CONFIG_MATCH;
                        pp_diag(ppi, ext, 1, "Locking PLL\n");
                        wrp->ops->locking_enable(ppi);
			/* FIXME: prepare timeout for PLL locking */
		}

		if (!(wrp->ha_conf & HA_RX_COHERENT) &&
		    (wrp->ha_peer_conf & HA_RX_COHERENT)) {
			/* We are a master, we wait for the slave to lock */
			wrp->L1SyncState = L1SYNC_CONFIG_MATCH;
		}
		break;

	case L1SYNC_CONFIG_MATCH: /* If slave, poll, if master, wait peer */
		if (wrp->ha_conf & HA_RX_COHERENT) {
			/* Slave -- FIXME: timeout for PLL locking */
			if (wrp->ops->locking_poll(ppi, 0) == WR_SPLL_READY) {
				pp_diag(ppi, ext, 1, "PLL is locked\n");
				wrp->ha_active |= HA_RX_COHERENT;
				wrp->ha_active |= HA_CONGRUENT;
				wrp->L1SyncState = L1SYNC_UP;
				do_xmit = 1;
			}
		}
		if (wrp->ha_peer_conf & HA_RX_COHERENT) {
			/* Master -- what about the slave timing out? */
			if (wrp->ha_peer_active & HA_RX_COHERENT) {
				wrp->ha_active |= HA_CONGRUENT;
				wrp->L1SyncState = L1SYNC_UP;
				do_xmit = 1;
			}
		}

	case L1SYNC_UP: /* FIXME: manage tacking-lost event */
		wrp->wrModeOn = 1;
		break;
	}

	if (do_xmit) {/* transmitting is simple */
		int len;

		pp_diag(ppi, ext, 1, "Sending signaling msg\n");
		len = ha_pack_signal(ppi);
		/* FIXME: check the destination MAC address */
		__send_and_log(ppi, len, PPM_SIGNALING, PP_NP_GEN);

		__pp_timeout_set(ppi, HA_TO_TX, to_tx); /* loop ever since */
	}

	/* Return the timeout for next invocation */
	return pp_next_delay_1(ppi, HA_TO_TX);
}

struct pp_ext_hooks pp_hooks = {
	.open = ha_open, /* misnamed: global init  */
	.init = ha_init, /* misnamed: port open */
	.handle_signaling = ha_handle_signaling,
	.calc_timeout = ha_calc_timeout,
};
