#include <ppsi/ppsi.h>
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

	pp_diag(ppi, ext, 2, "hook: %s (%i:%s) -- plen %i\n", __func__,
		ppi->state, ha_l1name[wrp->L1SyncState], plen);
	return 0;
}

/*
 * This hook is called by most states (master/slave etc) to do stuff
 * and send signalling messages; it returns the ext-specific timeout value
 */
static int ha_calc_timeout(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);

	pp_diag(ppi, ext, 2, "hook: %s (%i:%s)\n", __func__,
		ppi->state, ha_l1name[wrp->L1SyncState]);

	return 60*1000; /* infinite, until we implement it */
}

struct pp_ext_hooks pp_hooks = {
	.open = ha_open, /* misnamed: global init  */
	.init = ha_init, /* misnamed: port open */
	.handle_signaling = ha_handle_signaling,
	.calc_timeout = ha_calc_timeout,
};
