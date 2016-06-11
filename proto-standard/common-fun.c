/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */
#include <ppsi/ppsi.h>
#include "common-fun.h"
#include "../lib/network_types.h"
#include "../proto-ext-whiterabbit/wr-api.h" /* FIXME: phase_to_cf_units */

#ifdef CONFIG_ARCH_WRS
#define ARCH_IS_WRS 1
#else
#define ARCH_IS_WRS 0
#endif

void *msg_copy_header(MsgHeader *dest, MsgHeader *src)
{
	return memcpy(dest, src, sizeof(MsgHeader));
}

static void *__align_pointer(void *p)
{
	unsigned long ip, align = 0;

	ip = (unsigned long)p;
	if (ip & 3)
		align = 4 - (ip & 3);
	return p + align;
}

void pp_prepare_pointers(struct pp_instance *ppi)
{
	/*
	 * Horrible thing: when we receive vlan, we get standard eth header,
	 * but when we send we must fill the complete vlan header.
	 * So we reserve a different number of bytes.
	 */
	switch(ppi->proto) {
	case PPSI_PROTO_RAW:
		ppi->tx_offset = ETH_HLEN; /* 14, I know! */
		ppi->rx_offset = ETH_HLEN;
	#ifdef CONFIG_ARCH_WRPC
		ppi->tx_offset = 0; /* Currently, wrpc has a separate header */
		ppi->rx_offset = 0;
	#endif
		break;
	case PPSI_PROTO_VLAN:
		ppi->tx_offset = sizeof(struct pp_vlanhdr);
		/* Hack warning: with wrs we get the whole header */
		if (ARCH_IS_WRS)
			ppi->rx_offset = sizeof(struct pp_vlanhdr);
		else
			ppi->rx_offset = ETH_HLEN;
		break;
	case PPSI_PROTO_UDP:
		ppi->tx_offset = 0;
		ppi->rx_offset = 0;
		break;
	}
	ppi->tx_ptp = __align_pointer(ppi->__tx_buffer + ppi->tx_offset);
	ppi->rx_ptp = __align_pointer(ppi->__rx_buffer + ppi->rx_offset);

	/* Now that ptp payload is aligned, get back the header */
	ppi->tx_frame = ppi->tx_ptp - ppi->tx_offset;
	ppi->rx_frame = ppi->rx_ptp - ppi->rx_offset;

	if (0) { /* enable to verify... it works for me though */
		pp_printf("%p -> %p %p\n",
			  ppi->__tx_buffer, ppi->tx_frame, ppi->tx_ptp);
		pp_printf("%p -> %p %p\n",
			  ppi->__rx_buffer, ppi->rx_frame, ppi->rx_ptp);
	}
}

/* Called by listening, passive, slave, uncalibrated */
int st_com_execute_slave(struct pp_instance *ppi)
{
	int ret = 0;

	if (pp_hooks.execute_slave)
		ret = pp_hooks.execute_slave(ppi);
	if (ret == 1) /* done: just return */
		return 0;
	if (ret < 0)
		return ret;

	if (pp_timeout(ppi, PP_TO_ANN_RECEIPT)) {
		ppi->frgn_rec_num = 0;
		if (DSDEF(ppi)->clockQuality.clockClass != PP_CLASS_SLAVE_ONLY
		    && (ppi->role != PPSI_ROLE_SLAVE)) {
			ppi->next_state = PPS_MASTER;
		} else {
			ppi->next_state = PPS_LISTENING;
			pp_timeout_set(ppi, PP_TO_ANN_RECEIPT);
		}
	}
	return 0;
}

/* Called by this file, basically when an announce is got, all states */
static void st_com_add_foreign(struct pp_instance *ppi, unsigned char *buf)
{
	int i;
	MsgHeader *hdr = &ppi->received_ptp_header;

	/* Check if foreign master is already known */
	for (i = 0; i < ppi->frgn_rec_num; i++) {
		if (!memcmp(&hdr->sourcePortIdentity,
			    &ppi->frgn_master[i].port_id,
			    sizeof(hdr->sourcePortIdentity))) {
			/* already in Foreign master data set, update info */
			msg_copy_header(&ppi->frgn_master[i].hdr, hdr);
			msg_unpack_announce(buf, &ppi->frgn_master[i].ann);
			return;
		}
	}

	/* New foreign master */
	if (ppi->frgn_rec_num < PP_NR_FOREIGN_RECORDS)
		ppi->frgn_rec_num++;

	/* FIXME: replace the worst */
	i = ppi->frgn_rec_num - 1;

	/* Copy new foreign master data set from announce message */
	memcpy(&ppi->frgn_master[i].port_id,
	       &hdr->sourcePortIdentity, sizeof(hdr->sourcePortIdentity));

	/*
	 * header and announce field of each Foreign Master are
	 * useful to run Best Master Clock Algorithm
	 */
	msg_copy_header(&ppi->frgn_master[i].hdr, hdr);
	msg_unpack_announce(buf, &ppi->frgn_master[i].ann);

	pp_diag(ppi, bmc, 1, "New foreign Master %i added\n", i);
}


/* Called by slave and uncalibrated */
int st_com_slave_handle_announce(struct pp_instance *ppi, unsigned char *buf,
				 int len)
{
	if (len < PP_ANNOUNCE_LENGTH)
		return -1;

	/* st_com_add_foreign takes care of announce unpacking */
	st_com_add_foreign(ppi, buf);

	/*Reset Timer handling Announce receipt timeout*/
	pp_timeout_set(ppi, PP_TO_ANN_RECEIPT);

	ppi->next_state = bmc(ppi); /* got a new announce: run bmc */

	if (pp_hooks.handle_announce)
		pp_hooks.handle_announce(ppi);

	return 0;
}

/* Called by slave and uncalibrated */
int st_com_slave_handle_sync(struct pp_instance *ppi, unsigned char *buf,
			     int len)
{
	MsgHeader *hdr = &ppi->received_ptp_header;
	MsgSync sync;

	if (len < PP_SYNC_LENGTH)
		return -1;
	if (!(ppi->flags & PPI_FLAG_FROM_CURRENT_PARENT))
		return 0;

	/* t2 may be overriden by follow-up, cField is always valid */
	ppi->t2 = ppi->last_rcv_time;
	cField_to_TimeInternal(&ppi->cField, hdr->correctionfield);

	if ((hdr->flagField[0] & PP_TWO_STEP_FLAG) != 0) {
		ppi->flags |= PPI_FLAG_WAITING_FOR_F_UP;
		ppi->recv_sync_sequence_id = hdr->sequenceId;
		return 0;
	}
	msg_unpack_sync(buf, &sync);
	ppi->flags &= ~PPI_FLAG_WAITING_FOR_F_UP;
	to_TimeInternal(&ppi->t1,
			&sync.originTimestamp);
	if (GLBS(ppi)->delay_mech)
		pp_servo_got_psync(ppi);
	else
		pp_servo_got_sync(ppi);
	return 0;
}

int st_com_peer_handle_pres(struct pp_instance *ppi, unsigned char *buf,
			    int len)
{
	MsgPDelayResp resp;
	MsgHeader *hdr = &ppi->received_ptp_header;

	if (len < PP_PDELAY_RESP_LENGTH)
		return -1;

	msg_unpack_pdelay_resp(buf, &resp);

	if ((memcmp(&DSPOR(ppi)->portIdentity.clockIdentity,
		    &resp.requestingPortIdentity.clockIdentity,
		    PP_CLOCK_IDENTITY_LENGTH) == 0) &&
	    ((ppi->sent_seq[PPM_PDELAY_REQ]) ==
	     hdr->sequenceId) &&
	    (DSPOR(ppi)->portIdentity.portNumber ==
	     resp.requestingPortIdentity.portNumber) &&
	    (ppi->flags & PPI_FLAG_FROM_CURRENT_PARENT)) {

		to_TimeInternal(&ppi->t4, &resp.requestReceiptTimestamp);
		ppi->t6 = ppi->last_rcv_time;
		ppi->t6_cf = phase_to_cf_units(ppi->last_rcv_time.phase);
		if ((hdr->flagField[0] & PP_TWO_STEP_FLAG) != 0)
			ppi->flags |= PPI_FLAG_WAITING_FOR_RF_UP;
		else {
			ppi->flags &= ~PPI_FLAG_WAITING_FOR_RF_UP;
			/*
			 * Make sure responseOriginTimestamp is forced to 0
			 * for one-step responders
			 */
			memset(&ppi->t5, 0, sizeof(ppi->t5));
		}

		/* Save correctionField of pdelay_resp, see 11.4.3 d 3/4 */
		cField_to_TimeInternal(&ppi->cField, hdr->correctionfield);

	} else {
		pp_diag(ppi, frames, 2, "pp_pclock : "
			"PDelay Resp doesn't match PDelay Req\n");
	}
	return 0;
}


int st_com_peer_handle_pres_followup(struct pp_instance *ppi,
				     unsigned char *buf, int plen)
{
	MsgHeader *hdr = &ppi->received_ptp_header;
	MsgPDelayRespFollowUp respFllw;
	int e = 0;
	TimeInternal tmp;

	if (plen < PP_PDELAY_RESP_FOLLOW_UP_LENGTH)
		/* Ignore */
		return e;

	msg_unpack_pdelay_resp_follow_up(buf, &respFllw);

	if ((memcmp(&DSPOR(ppi)->portIdentity.clockIdentity,
		    &respFllw.requestingPortIdentity.clockIdentity,
		    PP_CLOCK_IDENTITY_LENGTH) == 0) &&
	    ((ppi->sent_seq[PPM_PDELAY_REQ]) ==
	     hdr->sequenceId) &&
	    (DSPOR(ppi)->portIdentity.portNumber ==
	     respFllw.requestingPortIdentity.portNumber) &&
	    (ppi->flags & PPI_FLAG_FROM_CURRENT_PARENT)) {

		to_TimeInternal(&ppi->t5,
				&respFllw.responseOriginTimestamp);
		/*
		 * Add correctionField of pdelay_resp_followup to
		 * cf of pdelay_resp (see 11.4.3 d 4)
		 */
		cField_to_TimeInternal(&tmp, hdr->correctionfield);
		add_TimeInternal(&ppi->cField, &ppi->cField, &tmp);

		if (pp_hooks.handle_presp)
			e = pp_hooks.handle_presp(ppi);
		else
			pp_servo_got_presp(ppi);
	} else {
		pp_diag(ppi, frames, 2, "%s: "
			"PDelay Resp F-up doesn't match PDelay Req\n",
			__func__);
	}
	return e;
}

int st_com_peer_handle_preq(struct pp_instance *ppi, unsigned char *buf,
			    int len)
{
	int e = 0;

	if (len < PP_PDELAY_REQ_LENGTH)
		return -1;

	if (pp_hooks.handle_preq)
		e = pp_hooks.handle_preq(ppi);
	if (e)
		return e;

	msg_issue_pdelay_resp(ppi, &ppi->last_rcv_time);
	msg_issue_pdelay_resp_followup(ppi, &ppi->last_snt_time);

	return 0;
}

/* Called by slave and uncalibrated */
int st_com_slave_handle_followup(struct pp_instance *ppi, unsigned char *buf,
				 int len)
{
	MsgFollowUp follow;
	int ret = 0;
	TimeInternal cField;

	MsgHeader *hdr = &ppi->received_ptp_header;

	if (len < PP_FOLLOW_UP_LENGTH)
		return -1;

	if (!(ppi->flags & PPI_FLAG_FROM_CURRENT_PARENT)) {
		pp_error("%s: Follow up message is not from current parent\n",
			__func__);
		return 0;
	}

	if (!(ppi->flags & PPI_FLAG_WAITING_FOR_F_UP)) {
		pp_error("%s: Slave was not waiting a follow up message\n",
			__func__);
		return 0;
	}

	if (ppi->recv_sync_sequence_id != hdr->sequenceId) {
		pp_error("%s: SequenceID %d doesn't match last Sync message %d\n",
				 __func__, hdr->sequenceId, ppi->recv_sync_sequence_id);
		return 0;
	}

	msg_unpack_follow_up(buf, &follow);
	ppi->flags &= ~PPI_FLAG_WAITING_FOR_F_UP;
	to_TimeInternal(&ppi->t1, &follow.preciseOriginTimestamp);

	/* Add correctionField in follow-up to sync correctionField, see 11.2 */
	cField_to_TimeInternal(&cField, hdr->correctionfield);
	add_TimeInternal(&ppi->cField, &ppi->cField, &cField);

	/* Call the extension; it may do it all and ask to return */
	if (pp_hooks.handle_followup)
		ret = pp_hooks.handle_followup(ppi, &ppi->t1, &ppi->cField);
	if (ret == 1)
		return 0;
	if (ret < 0)
		return ret;

	if (GLBS(ppi)->delay_mech)
		pp_servo_got_psync(ppi);
	else
		pp_servo_got_sync(ppi);

	return 0;
}

/* Called by master, listenting, passive. */
int st_com_master_handle_announce(struct pp_instance *ppi, unsigned char *buf,
				  int len)
{
	if (len < PP_ANNOUNCE_LENGTH)
		return -1;

	pp_diag(ppi, bmc, 2, "Announce message from another foreign master\n");

	st_com_add_foreign(ppi, buf);
	ppi->next_state = bmc(ppi); /* got a new announce: run bmc */

	if (pp_hooks.handle_announce)
		pp_hooks.handle_announce(ppi);

	return 0;
}

/*
 * Called by master, listenting, passive.
 * FIXME: this must be implemented to support one-step masters
 */
int st_com_master_handle_sync(struct pp_instance *ppi, unsigned char *buf,
			      int len)
{
	/* No more used: follow up is sent right after the corresponding sync */
	return 0;
}
