/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Copyright (C) 2014 GSI (www.gsi.de)
 * Author: Cesar Prados
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_slave(struct pp_instance *ppi, void *pkt, int plen)
{
	int e = 0; /* error var, to check errors in msg handling */
	struct msg_header_wire *hdr = &ppi->received_ptp_header;
	MsgDelayResp resp;

	if (ppi->is_new_state) {
		memset(&ppi->t1, 0, sizeof(ppi->t1));
		pp_servo_init(ppi);

		if (pp_hooks.new_slave)
			e = pp_hooks.new_slave(ppi, pkt, plen);
		if (e)
			goto out;
	}

	e  = pp_lib_may_issue_request(ppi);

	if (plen == 0)
		goto out;

	switch (msg_hdr_get_msg_type(hdr)) {

	case PPM_ANNOUNCE:
		e = st_com_slave_handle_announce(ppi, pkt, plen);
		break;

	case PPM_SYNC:
		e = st_com_slave_handle_sync(ppi, pkt, plen);
		break;

	case PPM_FOLLOW_UP:
		e = st_com_slave_handle_followup(ppi, pkt, plen);
		break;

	case PPM_DELAY_REQ:
		/* Being slave, we are not waiting for a delay request */
		break;

	case PPM_DELAY_RESP:
	{
		struct port_identity *pi;

		if (plen < PP_DELAY_RESP_LENGTH)
			break;

		msg_unpack_delay_resp(pkt, &resp);
		pi = &resp.requestingPortIdentity;

		if (delay_resp_is_mine(ppi, hdr, pi)) {
			to_TimeInternal(&ppi->t4, &resp.receiveTimestamp);
			/* Save delay resp cf in ppi->cField */
			cField_to_TimeInternal(&ppi->cField,
					       msg_hdr_get_cf(hdr));

			if (pp_hooks.handle_resp)
				e = pp_hooks.handle_resp(ppi);
			else
				pp_servo_got_resp(ppi);
			if (e)
				goto out;

			DSPOR(ppi)->logMinDelayReqInterval =
				msg_hdr_get_log_msg_intvl(hdr);
			pp_timeout_init(ppi); /* new value for logMin */
		} else {
			pp_diag(ppi, frames, 2, "pp_slave : "
			     "Delay Resp doesn't match Delay Req\n");
		}

		break;
	}
	case PPM_PDELAY_REQ:
		e = st_com_peer_handle_preq(ppi, pkt, plen);
		break;

	case PPM_PDELAY_RESP:
		e = st_com_peer_handle_pres(ppi, pkt, plen);
		break;

	case PPM_PDELAY_RESP_FOLLOW_UP:
		e = st_com_peer_handle_pres_followup(ppi, pkt, plen);
		break;
	default:
		/* disregard, nothing to do */
		break;
	}

out:
	if (e == 0)
		e = st_com_execute_slave(ppi);

	switch(e) {
	case PP_SEND_OK: /* 0 */
		break;
	case PP_SEND_ERROR:
		ppi->next_state = PPS_FAULTY;
		break;
	case PP_SEND_NO_STAMP:
		/* nothing, just keep the ball rolling */
		e = 0;
		break;
	}

	if (ppi->next_state != ppi->state) {
		pp_servo_init(ppi);
		return e;
	}
	ppi->next_delay = pp_next_delay_2(ppi,
					  PP_TO_ANN_RECEIPT, PP_TO_REQUEST);
	return e;
}
