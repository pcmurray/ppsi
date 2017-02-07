/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on ptp-noposix project (see AUTHORS for details)
 *
 * Released to the public domain
 */

#include <ppsi/ppsi.h>
#include "wr-api.h"

int wr_link_on(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	int e = 0;
	
	//struct wr_dsport *wrp_hsr0 = WR_DSPOR(INST(ppi->glbs, 0));
	//struct wr_dsport *wrp_hsr1= WR_DSPOR(INST(ppi->glbs, 1));

	if (ppi->is_new_state) {
		wrp->wrModeOn = TRUE;
// 		if(ppi->slave_prio == 0)
			wrp->ops->enable_ptracker(ppi); // might need change

		if (wrp->wrMode == WR_MASTER) {
			e = msg_issue_wrsig(ppi, WR_MODE_ON);
			ppi->master_only = 0;
			ppi->slave_only = 0;
			ppi->backup_only = 1;
		}

		wrp->parentWrModeOn = TRUE;
		wrp->wrPortState = WRS_WR_LINK_ON;
	}

	if (e != 0)
		return -1;

	wrp->wrPortState = WRS_IDLE;

	if (wrp->wrMode == WR_SLAVE)
	{
		ppi->next_state = PPS_SLAVE;
		ppi->glbs->holdover = 0;
	}
	else
	{
		ppi->next_state = PPS_SLAVE;
		wrp->ops->locking_enable(ppi);
	}
	if (!ppi->is_HSR) ppi->next_state = PPS_MASTER;
	return 0;
}
