/*
 * Copyright (C) 2014 GSI (www.gsi.de)
 * Author: Cesar Prados
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

int pp_pclock(struct pp_instance *ppi, unsigned char *pkt, int plen)
{

	int e = 0; /* error var, to check errors in msg handling */
	MsgHeader *hdr = &ppi->received_ptp_header;
        MsgPDelayResp resp;
        MsgPDelayRespFollowUp respFllw;
	int d1, d2;

	if (ppi->is_new_state) {
                pp_servo_init(ppi);

                if (pp_hooks.new_slave)
                        e = pp_hooks.new_slave(ppi, pkt, plen);
                if (e)
                        goto out;

                ppi->waiting_for_follow = FALSE;
                ppi->waiting_for_resp_follow = FALSE;

                pp_timeout_restart_annrec(ppi);
		pp_timeout_rand(ppi, PP_TO_PDELAYREQ,
                                DSPOR(ppi)->logMinPDelayReqInterval);
	}

        if (plen == 0)
                goto out;

        switch (hdr->messageType) {

        case PPM_ANNOUNCE:
		e = st_com_slave_handle_announce(ppi, pkt, plen);
		break;

	case PPM_SYNC:
		e = st_com_slave_handle_sync(ppi, pkt, plen);
		break;

	case PPM_FOLLOW_UP:
		e = st_com_slave_handle_followup(ppi, pkt, plen);
		break;

        case PPM_PDELAY_REQ:
                e = (plen < PP_PDELAY_RESP_LENGTH);
                if (e)
                        break;

                msg_copy_header(&ppi->pdelay_req_hdr,
				&ppi->received_ptp_header);
		msg_issue_pdelay_resp(ppi, &ppi->last_rcv_time);
		msg_issue_pdelay_resp_followup(ppi, &ppi->last_snt_time);
		break;

        case PPM_PDELAY_RESP:
		e = (plen < PP_PDELAY_RESP_LENGTH);
                if (e)
			break;

                msg_unpack_pdelay_resp(pkt, &resp);

		if ((memcmp(&DSPOR(ppi)->portIdentity.clockIdentity,
			&resp.requestingPortIdentity.clockIdentity,
			PP_CLOCK_IDENTITY_LENGTH) == 0) &&
			((ppi->sent_seq[PPM_PDELAY_REQ]) ==
				hdr->sequenceId) &&
			(DSPOR(ppi)->portIdentity.portNumber ==
			        resp.requestingPortIdentity.portNumber) &&
                        ppi->is_from_cur_par) {

			to_TimeInternal(&ppi->t2, &resp.requestReceiptTimestamp);
			ppi->t4 = ppi->last_rcv_time;

		} else {
			pp_diag(ppi, frames, 2, "pp_pclock : "
			     "PDelay Resp doesn't match PDelay Req\n");
		}
                break;

        case PPM_PDELAY_RESP_FOLLOW_UP:
		e = (plen < PP_PDELAY_RESP_FOLLOW_UP_LENGTH);
                if (e)
			break;

                msg_unpack_pdelay_resp_follow_up(pkt, &respFllw);

		if ((memcmp(&DSPOR(ppi)->portIdentity.clockIdentity,
			&respFllw.requestingPortIdentity.clockIdentity,
			PP_CLOCK_IDENTITY_LENGTH) == 0) &&
			((ppi->sent_seq[PPM_PDELAY_REQ]) ==
				hdr->sequenceId) &&
			(DSPOR(ppi)->portIdentity.portNumber ==
			respFllw.requestingPortIdentity.portNumber) &&
                                ppi->is_from_cur_par) {

		        to_TimeInternal(&ppi->t3, &respFllw.responseOriginTimestamp);

		        //pp_servo_got_resp(ppi);

	        } else {
                        pp_diag(ppi, frames, 2, "pp_pclock : "
			                "Delay Resp Follow doesn't match Delay Req\n");
                }
	        break;

        default:
                /* disregard, nothing to do */
                break;
        }

out:
        if (e == 0)
                e = st_com_execute_slave(ppi);

        if (pp_timeout_z(ppi, PP_TO_PDELAYREQ)) {
		e = msg_issue_pdelay_req(ppi);

		ppi->t3 = ppi->last_snt_time;

		/* Restart the timeout for next time */
		pp_timeout_rand(ppi, PP_TO_PDELAYREQ,
				DSPOR(ppi)->logMinPDelayReqInterval);
	}

	if (e) {
		ppi->next_state = PPS_FAULTY;
		return 0;
	}

	/* Leaving this state */
	if (ppi->next_state != ppi->state) {
		pp_timeout_clr(ppi, PP_TO_ANN_RECEIPT);
		pp_timeout_clr(ppi, PP_TO_PDELAYREQ);

		pp_servo_init(ppi);
	}

	d1 = d2 = pp_ms_to_timeout(ppi, PP_TO_ANN_RECEIPT);
	if (ppi->timeouts[PP_TO_PDELAYREQ])
		d2 = pp_ms_to_timeout(ppi, PP_TO_PDELAYREQ);

	ppi->next_delay = d1 < d2 ? d1 : d2;

        return 0;
}
