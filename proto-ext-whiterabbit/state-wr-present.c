/*
 * Aurelio Colosimo for CERN, 2012 -- GNU LGPL v2.1 or later
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 */

#include <ppsi/ppsi.h>
#include "wr_api.h"

int wr_present(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	/* FIXME implementation */
	if (ppi->is_new_state) {
		DSPOR(ppi)->portState = PPS_SLAVE;
		DSPOR(ppi)->wrPortState = WRS_PRESENT;
		ppi->next_delay = PP_DEFAULT_NEXT_DELAY_MS;
	}
	return 0;
}