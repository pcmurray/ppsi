/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */
#include <ppsi/ppsi.h>
#include "common-fun.h"

static TimeInternal picos_to_ts(int64_t picos)
{
	uint64_t nsec, phase;
	TimeInternal ts;

	nsec = picos;
	phase = __div64_32(&nsec, 1000);

	ts.nanoseconds = __div64_32(&nsec, PP_NSEC_PER_SEC);
	ts.seconds = nsec; /* after the division */
	ts.phase = phase;
	return ts;
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
	ppi->tx_ptp = __align_pointer(pp_get_payload(ppi, ppi->tx_buffer));
	ppi->rx_ptp = __align_pointer(pp_get_payload(ppi, ppi->rx_buffer));

	/* Now that ptp payload is aligned, get back the header */
	ppi->tx_frame = pp_get_header(ppi, ppi->tx_ptp);
	ppi->rx_frame = pp_get_header(ppi, ppi->rx_ptp);

	if (0) { /* enable to verify... it works for me though */
		pp_printf("%p -> %p %p\n",
			  ppi->tx_buffer, ppi->tx_frame, ppi->tx_ptp);
		pp_printf("%p -> %p %p\n",
			  ppi->rx_buffer, ppi->rx_frame, ppi->rx_ptp);
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

	if (pp_timeout_z(ppi, PP_TO_ANN_RECEIPT)) {
		ppi->frgn_rec_num = 0;
		if (DSDEF(ppi)->clockQuality.clockClass != PP_CLASS_SLAVE_ONLY
		    && !ppi->slave_only) {
			ppi->next_state = PPS_MASTER;
		} else {
			ppi->next_state = PPS_LISTENING;
			pp_timeout_restart_annrec(ppi);
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
			pp_diag(ppi, bmc, 1, "Updated existing foreign Master %i added\n", i);
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
	pp_timeout_restart_annrec(ppi);

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
	if (!ppi->is_from_cur_par)
		return 0;

	/* t2 may be overriden by follow-up, cField is always valid */
	ppi->t2 = ppi->last_rcv_time;
	ppi->sync_ingress = ppi->t2;
	cField_to_TimeInternal(&ppi->cField, hdr->correctionfield);

	if ((hdr->flagField[0] & PP_TWO_STEP_FLAG) != 0) {
		ppi->waiting_for_follow = TRUE;
		ppi->recv_sync_sequence_id = hdr->sequenceId;
		return 0;
	}
	msg_unpack_sync(buf, &sync);
	ppi->waiting_for_follow = FALSE;
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
			resp.requestingPortIdentity.portNumber) /*&&
		ppi->is_from_cur_par*/) { /* GUTI BAD HACK */

		to_TimeInternal(&ppi->t4, &resp.requestReceiptTimestamp);
		ppi->t6 = ppi->last_rcv_time;
		ppi->t6_cf = phase_to_cf_units(ppi->last_rcv_time.phase);
		ppi->waiting_for_resp_follow = TRUE;

		/* todo: in one clock the presp carries t5-t4 */

	} else {
		pp_diag(ppi, frames, 2, "pp_pclock : "
		     "PDelay Resp doesn't match PDelay Req\n");
	}
	return 0;
}

int st_com_peer_handle_preq(struct pp_instance *ppi, unsigned char *buf,
			     int len)
{
	if (len < PP_PDELAY_REQ_LENGTH)
		return -1;

	msg_copy_header(&ppi->pdelay_req_hdr,
			&ppi->received_ptp_header);
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

	MsgHeader *hdr = &ppi->received_ptp_header;

	if (len < PP_FOLLOW_UP_LENGTH)
		return -1;

	if (!ppi->is_from_cur_par) {
		pp_error("%s: Follow up message is not from current parent\n",
			__func__);
		return 0;
	}

	if (!ppi->waiting_for_follow) {
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
	ppi->waiting_for_follow = FALSE;
	to_TimeInternal(&ppi->t1, &follow.preciseOriginTimestamp);

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

/*
 * Called by Transparent Clocks.
 * FIXME: this must be implemented to support one-step masters
 */
int tc_send_fwd_ann(struct pp_instance *ppi, unsigned char *pkt,
											int plen)
{
	__send_and_log(ppi, plen, PPM_ANNOUNCE, PP_NP_GEN);

	return 1;
}

/*
 * Called by Transparent Clocks.
 * FIXME: this must be implemented to support one-step masters
 */
int tc_send_fwd_sync(struct pp_instance *ppi, unsigned char *pkt,
											int plen)
{
	__send_and_log(ppi, plen, PPM_SYNC, PP_NP_EVT);
	ppi->sync_egress = ppi->last_snt_time; /* egress time for sync */

	return 1;
}

/*
 * Called by Transparent Clocks.
 * FIXME: this must be implemented to support one-step masters
 */
int tc_send_fwd_followup(struct pp_instance *ppi, unsigned char *pkt,
												int plen)
{
	TimeInternal residence_time; /* Transp. Clocks - Residence Time + Link Delay */
	TimeInternal delay_ms; /* Transp. Clocks - Link Delay measured with pDelay */

	/* adding residence time and link delay to correction field for p2p */
	residence_time.seconds = 0;
	residence_time.nanoseconds = 0;
	residence_time.phase = 0;

	/* FIXME: cField is not completely following the standard, but since
	 * we own the field, convert and use the same "wrong" way in all sides, it works.
	 * This should be changed to comply the PTP standard. */
	sub_TimeInternal(&residence_time, &ppi->sync_egress, &ppi->sync_ingress);
	add_TimeInternal(&residence_time, &residence_time, &ppi->p2p_cField);
	residence_time.phase += ppi->p2p_cField.phase + 
		(ppi->sync_egress.phase - ppi->sync_ingress.phase);
	delay_ms = picos_to_ts(ppi->l_delay_ingress);
	add_TimeInternal(&residence_time, &residence_time, &delay_ms);
	residence_time.nanoseconds += residence_time.seconds * 1e9;
	residence_time.phase += delay_ms.phase;

	update_followup_cField(ppi, residence_time);

	__send_and_log(ppi, plen, PPM_FOLLOW_UP, PP_NP_EVT);

	return 1;
}

/*
 * Called by Transparent Clocks.
 * It copies the message to forward to all the other ports
 * tc_send_fwd_announce() sends the message in the other ppi instance
 * FIXME: this must be implemented to support one-step masters
 */
int tc_forward_ann(struct pp_instance *ppi, unsigned char *pkt,
											int plen)
{
	int j, max_ports = 8;
	struct pp_instance *ppi_aux;

	for (j = 0; j < max_ports; j++) {
		ppi_aux = INST(ppi->glbs, j);
		if(WR_DSPOR(ppi_aux)->linkUP
					&& (ppi->port_idx != ppi_aux->port_idx)){
			memcpy(ppi_aux->tx_buffer, ppi->rx_buffer,
				PP_MAX_FRAME_LENGTH);
			memcpy(ppi_aux->tx_ptp, ppi->rx_ptp, 
				PP_MAX_FRAME_LENGTH);
			tc_send_fwd_ann(ppi_aux, pkt, plen);
		}
	}

	return 1;
}

/*
 * Called by Transparent Clocks.
 * It copies the message to forward to all the other ports
 * tc_send_fwd_sync() sends the message in the other ppi instance
 * FIXME: this must be implemented to support one-step masters
 */
int tc_forward_sync(struct pp_instance *ppi, unsigned char *pkt,
											int plen)
{
	int j, max_ports = 8;
	struct pp_instance *ppi_aux;

	for (j = 0; j < max_ports; j++) {
		ppi_aux = INST(ppi->glbs, j);
		if(WR_DSPOR(ppi_aux)->linkUP
					&& (ppi->port_idx != ppi_aux->port_idx)){
			memcpy(ppi_aux->tx_buffer, ppi->rx_buffer,
				PP_MAX_FRAME_LENGTH);
			memcpy(ppi_aux->tx_ptp, ppi->rx_ptp,
				PP_MAX_FRAME_LENGTH);
			tc_send_fwd_sync(ppi_aux, pkt, plen);
		}
	}

	return 1;
}

/*
 * Called by Transparent Clocks.
 * It copies the message to forward to all the other ports
 * tc_send_fwd_followup() sends the message in the other ppi instance
 * FIXME: this must be implemented to support one-step masters
 */
int tc_forward_followup(struct pp_instance *ppi, unsigned char *pkt,
											int plen)
{
	int j, max_ports = 8;
	struct pp_instance *ppi_aux;
	MsgHeader *hdr = &ppi->received_ptp_header;

	ppi->p2p_cField.nanoseconds = hdr->correctionfield.msb;
	ppi->p2p_cField.phase = hdr->correctionfield.lsb;

	for (j = 0; j < max_ports; j++) {
		ppi_aux = INST(ppi->glbs, j);
		if(WR_DSPOR(ppi_aux)->linkUP
					&& (ppi->port_idx != ppi_aux->port_idx)){
			memcpy(ppi_aux->tx_buffer, ppi->rx_buffer,
				PP_MAX_FRAME_LENGTH);
			memcpy(ppi_aux->tx_ptp, ppi->rx_ptp,
				PP_MAX_FRAME_LENGTH);
			ppi_aux->sync_ingress = ppi->sync_ingress;
			ppi_aux->l_delay_ingress = ppi->link_delay;
			ppi_aux->p2p_cField = ppi->p2p_cField;
			tc_send_fwd_followup(ppi_aux, pkt, plen);
		}
	}

	return 1;
}

