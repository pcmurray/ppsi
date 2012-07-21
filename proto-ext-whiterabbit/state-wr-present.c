/*
 * Aurelio Colosimo for CERN, 2012 -- GNU LGPL v2.1 or later
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 */

#include <ppsi/ppsi.h>
#include "wr-api.h"
#include "common-fun.h"

int wr_present(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	int e = 0;

	MsgSignaling wrsig_msg;

	if (ppi->is_new_state) {
		DSPOR(ppi)->portState = PPS_UNCALIBRATED;
		DSPOR(ppi)->wrPortState = WRS_PRESENT;
		DSPOR(ppi)->wrMode = WR_SLAVE;
		pp_timer_start(DSPOR(ppi)->wrStateTimeout / 1000,
			ppi->timers[PP_TIMER_WRS_PRESENT]);
		st_com_restart_annrec_timer(ppi);
		e = msg_issue_wrsig(ppi, SLAVE_PRESENT);
	}

	if (pp_timer_expired(ppi->timers[PP_TIMER_WRS_PRESENT])) {
		ppi->next_state = PPS_LISTENING;
		DSPOR(ppi)->wrMode = NON_WR;
		DSPOR(ppi)->wrPortState = WRS_IDLE;
		goto state_updated;
	}

	if (plen == 0)
		goto no_incoming_msg;

	if (ppi->msg_tmp_header.messageType == PPM_SIGNALING) {

		msg_unpack_wrsig(ppi, pkt, &wrsig_msg,
			 &(DSPOR(ppi)->msgTmpWrMessageID));

		if (DSPOR(ppi)->msgTmpWrMessageID == LOCK)
			ppi->next_state = WRS_S_LOCK;
	}

no_incoming_msg:
	if (e == 0)
		st_com_execute_slave(ppi, 0);
	else
		ppi->next_state = PPS_FAULTY;

state_updated:
	if (ppi->next_state != ppi->state) {
		pp_timer_stop(ppi->timers[PP_TIMER_WRS_PRESENT]);
		pp_timer_stop(ppi->timers[PP_TIMER_ANN_RECEIPT]);
	}

	ppi->next_delay = PP_DEFAULT_NEXT_DELAY_MS;

	return e;
}
