/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Copyright (C) 2014 GSI (www.gsi.de)
 * Author: Cesar Prados
 * Originally based on PTPd project v. 2.1.0
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

static int slave_handle_response(struct pp_instance *ppi, unsigned char *pkt,
				 int plen);

static pp_action *actions[] = {
	[PPM_SYNC]		= st_com_slave_handle_sync,
	[PPM_DELAY_REQ]		= 0,
#if CONFIG_HAS_P2P
	[PPM_PDELAY_REQ]	= st_com_peer_handle_preq,
	[PPM_PDELAY_RESP]	= st_com_peer_handle_pres,
	[PPM_PDELAY_R_FUP]	= st_com_peer_handle_pres_followup,
#endif
	[PPM_FOLLOW_UP]		= st_com_slave_handle_followup,
	[PPM_DELAY_RESP]	= slave_handle_response,
	[PPM_ANNOUNCE]		= st_com_slave_handle_announce,
	[PPM_SIGNALING]		= 0, /* pp_hooks.handle_signaling */
};

static int slave_handle_response(struct pp_instance *ppi, unsigned char *pkt,
				 int plen)
{
	int e = 0;
	MsgHeader *hdr = &ppi->received_ptp_header;
	MsgDelayResp resp;

	if (plen < PP_DELAY_RESP_LENGTH)
		return 0;

	msg_unpack_delay_resp(pkt, &resp);

	if ((memcmp(&DSPOR(ppi)->portIdentity.clockIdentity,
		    &resp.requestingPortIdentity.clockIdentity,
		    PP_CLOCK_IDENTITY_LENGTH) != 0) ||
	    ((ppi->sent_seq[PPM_DELAY_REQ]) !=
	     hdr->sequenceId) ||
	    (DSPOR(ppi)->portIdentity.portNumber !=
	     resp.requestingPortIdentity.portNumber) ||
	    !(ppi->flags & PPI_FLAG_FROM_CURRENT_PARENT)) {
		pp_diag(ppi, frames, 2, "pp_slave : "
			"Delay Resp doesn't match Delay Req\n");
		return 0;
	}

	to_TimeInternal(&ppi->t4, &resp.receiveTimestamp);
	/* Save delay resp cf in ppi->cField */
	cField_to_TimeInternal(&ppi->cField,
			       hdr->correctionfield);

	if (pp_hooks.handle_resp)
		e = pp_hooks.handle_resp(ppi);
	else
		pp_servo_got_resp(ppi);
	if (e)
		return e;

	if (DSPOR(ppi)->logMinDelayReqInterval !=
	    hdr->logMessageInterval) {
		DSPOR(ppi)->logMinDelayReqInterval =
			hdr->logMessageInterval;
		/* new value for logMin */
		pp_timeout_init(ppi);
	}
	return 0;
}

int pp_slave(struct pp_instance *ppi, unsigned char *pkt, int plen)
{
	int e = 0; /* error var, to check errors in msg handling */
	MsgHeader *hdr = &ppi->received_ptp_header;

	if (ppi->is_new_state) {
		memset(&ppi->t1, 0, sizeof(ppi->t1));
		pp_servo_init(ppi);

		if (pp_hooks.new_slave)
			e = pp_hooks.new_slave(ppi, pkt, plen);
		if (e)
			goto out;
	}

	e  = pp_lib_may_issue_request(ppi);

	/*
	 * The management of messages is now table-driven,
	 * and an extension can provide a hook for signaling.
	 */
	actions[PPM_SIGNALING] = pp_hooks.handle_signaling;
	if (plen && hdr->messageType < ARRAY_SIZE(actions)
	    && actions[hdr->messageType]) {
		e = actions[hdr->messageType](ppi, pkt, plen);
	} else {
		if (plen)
			pp_diag(ppi, frames, 1, "Ignored frame\n");
	}
	if (e)
		goto out;


out:
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

	if (pp_hooks.calc_timeout) {
		/* The extension may manage its own timeout, and do stuff */
		int ext_to = pp_hooks.calc_timeout(ppi);
		if (ext_to < ppi->next_delay)
			ppi->next_delay = ext_to;
	}

	return e;
}

