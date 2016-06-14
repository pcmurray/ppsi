/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>
#include "common-fun.h"

/* Unpack header from in buffer to receieved_ptp_header field */
int msg_unpack_header(struct pp_instance *ppi, void *_buf, int plen)
{
	struct msg_header_wire *hdr = &ppi->received_ptp_header;
	struct msg_header_wire *buf = _buf;
	struct port_identity pid;

	msg_hdr_copy(hdr, buf);

	/*
	 * 9.5.1:
	 * Only PTP messages where the domainNumber field of the PTP message
	 * header (see 13.3.2.5) is identical to the defaultDS.domainNumber
	 * shall be accepted for processing by the protocol.
	 */
	if (msg_hdr_get_msg_dn(hdr) != GDSDEF(GLBS(ppi))->domainNumber)
		return -1;

	/*
	 * Alternate masters (17.4) not supported
	 * 17.4.2, NOTE:
	 * A slave node that does not want to use information from alternate
	 * masters merely ignores all messages with alternateMasterFlag TRUE.
	 */
	if (msg_hdr_get_flag(hdr, PP_ALTERNATE_MASTER_FLAG))
		return -1;

	/*
	 * If the message is from the same port that sent it, we should
	 * discard it (9.5.2.2)
	 */
	msg_hdr_get_src_port_id(&pid, hdr);
	if (!port_id_cmp(&pid, &DSPOR(ppi)->portIdentity))
		return -1;

	/*
	 * This FLAG_FROM_CURRENT_PARENT must be killed. Meanwhile, say it's
	 * from current parent if we have no current parent, so the rest works
	 */
	msg_hdr_get_src_port_id(&pid, hdr);
	if (!DSPAR(ppi)->parentPortIdentity.portNumber ||
	    !port_id_cmp(&DSPAR(ppi)->parentPortIdentity, &pid))
		ppi->flags |= PPI_FLAG_FROM_CURRENT_PARENT;
	else
		ppi->flags &= ~PPI_FLAG_FROM_CURRENT_PARENT;
	return 0;
}

/* Pack header message into out buffer of ppi */
void msg_pack_header(struct pp_instance *ppi, void *buf)
{
	uint8_t flags[2] = { 0, 0, };

	/*
	 * (spec annex D and F),
	 * log_msg_intvl is the default value (spec Table 24)
	 */
	msg_hdr_init(buf, ppi, flags, 0x7f);
}

/* Pack Sync message into out buffer of ppi */
static void msg_pack_sync(struct pp_instance *ppi, Timestamp *orig_tstamp)
{
	void *buf;
	timestamp_wire *ts;
	uint16_t s;

	buf = ppi->tx_ptp;
	ts = buf + 34;

	s = ppi->sent_seq[PPM_SYNC]++;
	msg_hdr_prepare(ppi->tx_ptp, PPM_SYNC, PP_SYNC_LENGTH, s, 0,
			DSPOR(ppi)->logSyncInterval);
	/* We're a two step clock, set relevant flag in sync (see Table 20) */
	msg_hdr_set_flag(ppi->tx_ptp, PP_TWO_STEP_FLAG, 1);
	msg_hdr_set_cf(ppi->tx_ptp, 0ULL);
	/* Sync message */
	timestamp_internal_to_wire(ts, orig_tstamp);
}

/* Unpack Sync message from in buffer */
void msg_unpack_sync(void *buf, MsgSync *sync)
{
	timestamp_wire *ts = buf + 34;

	timestamp_wire_to_internal(&sync->originTimestamp, ts);
}

/* Pack Announce message into out buffer of ppi */
static int msg_pack_announce(struct pp_instance *ppi)
{
	struct msg_announce_wire *buf;
	struct msg_announce_body_wire *body;
	struct msg_header_wire *hdr;
	struct timestamp_wire tsw;
	uint16_t s;
	struct DSTimeProperties *prop = DSPRO(ppi);
	const uint8_t flags_mask[] = { 0xff, 0xff, };
	const uint8_t flags[] = { 0, prop->flags, };

	buf = ppi->tx_ptp;
	hdr = msg_announce_get_header(buf);
	body = msg_announce_get_body(buf);
	s = ppi->sent_seq[PPM_ANNOUNCE]++;
	msg_hdr_prepare(hdr, PPM_ANNOUNCE, PP_ANNOUNCE_LENGTH, s, 5,
			DSPOR(ppi)->logAnnounceInterval);
	/* Table 21 */
	msg_hdr_set_cf(hdr, 0ULL);

	/*
	 * set byte 1 of flags taking it from timepropertiesDS' flags field,
	 * see 13.3.2.6, Table 20
	 */
	msg_hdr_set_flags(hdr, flags_mask, flags);

	/* Announce message */
	memset(&tsw, 0, sizeof(tsw));
	msg_announce_set_origts(body, &tsw);
	msg_announce_set_utc_offs(body, DSPRO(ppi)->currentUtcOffset);
	msg_announce_set_gm_p1(body, DSPAR(ppi)->grandmasterPriority1);
	msg_announce_set_gm_cq(body, &DSPAR(ppi)->grandmasterClockQuality);
	msg_announce_set_gm_p2(body, DSPAR(ppi)->grandmasterPriority2);
	msg_announce_set_gm_cid(body, &DSPAR(ppi)->grandmasterIdentity);
	msg_announce_set_steps_removed(body, DSCUR(ppi)->stepsRemoved);
	msg_announce_set_ts(body, DSPRO(ppi)->timeSource);

	if (pp_hooks.pack_announce)
		return pp_hooks.pack_announce(ppi);
	return PP_ANNOUNCE_LENGTH;
}

/* Pack Follow Up message into out buffer of ppi*/
static void msg_pack_follow_up(struct pp_instance *ppi, Timestamp *prec_orig_tstamp)
{
	void *buf;
	/* sentSyncSequenceId has already been incremented in msg_issue_sync */
	uint16_t s = ppi->sent_seq[PPM_SYNC];
	struct timestamp_wire *ts;

	buf = ppi->tx_ptp;
	ts = buf + 34;

	msg_hdr_prepare(ppi->tx_ptp, PPM_FOLLOW_UP, PP_FOLLOW_UP_LENGTH, s, 2,
			DSPOR(ppi)->logSyncInterval);

	/* Follow Up message */
	timestamp_internal_to_wire(ts, prec_orig_tstamp);
}

/* Pack PDelay Follow Up message into out buffer of ppi*/
void msg_pack_pdelay_resp_follow_up(struct pp_instance *ppi,
				    struct msg_header_wire * hdr,
				    Timestamp * prec_orig_tstamp)
{
	void *buf;
	timestamp_wire *ts;
	uint64_wire *cf;
	uint64_t v;

	buf = ppi->tx_ptp;
	ts = buf + 34;
	cf = buf + 8;

	/* header */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x0A;

	*(uint16_t *) (buf + 2) = htons(PP_PDELAY_RESP_LENGTH);
	*(uint8_t *) (buf + 4) = msg_hdr_get_msg_dn(hdr);
	/* copy the correction field, 11.4.3 c.3) */
	v = msg_hdr_get_cf(hdr);
	uint64_internal_to_wire(cf, &v);

	*(uint16_t *) (buf + 30) = htons(msg_hdr_get_msg_seq_id(hdr));
	*(uint8_t *) (buf + 32) = 0x05;	/* controlField */

	/* requestReceiptTimestamp */
	timestamp_internal_to_wire(ts, prec_orig_tstamp);

	/* requestingPortIdentity, FIXME: use proper sizeof() */
	memcpy((buf + 44), &hdr, 10);
}

/* Unpack FollowUp message from in buffer of ppi to internal structure */
void msg_unpack_follow_up(void *buf, MsgFollowUp *flwup)
{
	struct timestamp_wire *ts = buf + 34;

	timestamp_wire_to_internal(&flwup->preciseOriginTimestamp, ts);
}

/* Unpack PDelayRespFollowUp message from in buffer of ppi to internal struct */
void msg_unpack_pdelay_resp_follow_up(void *buf,
				      MsgPDelayRespFollowUp * pdelay_resp_flwup)
{
	struct timestamp_wire *ts = buf + 34;

	timestamp_wire_to_internal(&pdelay_resp_flwup->responseOriginTimestamp,
				   ts);
	memcpy(&pdelay_resp_flwup->requestingPortIdentity.clockIdentity,
	       (buf + 44), PP_CLOCK_IDENTITY_LENGTH);
	pdelay_resp_flwup->requestingPortIdentity.portNumber =
	    htons(*(uint16_t *) (buf + 52));
}

/* pack DelayReq message into out buffer of ppi */
static void msg_pack_delay_req(struct pp_instance *ppi, Timestamp *orig_tstamp)
{
	void *buf;
	struct timestamp_wire *ts;
	uint16_t s = ppi->sent_seq[PPM_DELAY_REQ]++;

	buf = ppi->tx_ptp;
	ts = buf + 34;

	/* logMessageInterval is 0x7f, see Table 24 p. 128 */
	msg_hdr_prepare(ppi->tx_ptp, PPM_DELAY_REQ,
			PP_DELAY_REQ_LENGTH, s, 1, 0x7f);
	/* 11.3.2, 3, i: correctionField is 0 */
	msg_hdr_set_cf(ppi->tx_ptp, 0);

	/* Delay_req message */
	timestamp_internal_to_wire(ts, orig_tstamp);
}

/* pack DelayReq message into out buffer of ppi */
void msg_pack_pdelay_req(struct pp_instance *ppi, Timestamp * orig_tstamp)
{
	void *buf;
	struct timestamp_wire *ts;
	struct msg_header_wire *hdr;

	buf = ppi->tx_ptp;
	ts = buf + 34;
	hdr = buf;

	/* changes in header 11.4.3 */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x02;

	*(uint16_t *) (buf + 2) = htons(PP_PDELAY_REQ_LENGTH);
	ppi->sent_seq[PPM_DELAY_REQ]++;

	/* Reset all flags (see Table 20) */
	msg_hdr_reset_flags(hdr);

	/* TO DO, 11.4.3 a.1) if synthed peer-to-peer TC */
	/* *(char *)(buf + 4) = 0 .- not sythonized / X synt domain */

	memset((buf + 8), 0, 8);
	*(uint16_t *) (buf + 30) = htons(ppi->sent_seq[PPM_PDELAY_REQ]);
	*(uint8_t *) (buf + 32) = 0x05;

	/* Table 23 */
	*(int8_t *) (buf + 33) = 0x7F;

	/* PDelay_req message */
	timestamp_internal_to_wire(ts, orig_tstamp);
}

/* pack PDelayResp message into OUT buffer of ppi */
void msg_pack_pdelay_resp(struct pp_instance *ppi,
			  struct msg_header_wire * hdr, Timestamp * rcv_tstamp)
{
	void *buf;
	struct timestamp_wire *ts;

	buf = ppi->tx_ptp;
	ts = buf + 34;

	/* header */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x03;

	*(uint16_t *) (buf + 2) = htons(PP_PDELAY_RESP_LENGTH);
	*(uint8_t *) (buf + 4) = msg_hdr_get_msg_dn(hdr);
	/* We're a two step clock, set relevant flag in sync (see Table 20) */
	msg_hdr_reset_flags(ppi->tx_ptp);
	msg_hdr_set_flag(ppi->tx_ptp, PP_TWO_STEP_FLAG, 1);
	/* set 0 the correction field, 11.4.3 c.3) */
	memset((buf + 8), 0, 8);

	*(uint16_t *) (buf + 30) = htons(msg_hdr_get_msg_seq_id(hdr));
	*(uint8_t *) (buf + 32) = 0x05;	/* controlField */

	/* requestReceiptTimestamp */
	
	timestamp_internal_to_wire(ts, rcv_tstamp);
	/* requestingPortIdentity */
	memcpy((buf + 44), &hdr->spid, sizeof(hdr->spid));
}

/* pack DelayResp message into OUT buffer of ppi */
static void msg_pack_delay_resp(struct pp_instance *ppi,
				struct msg_header_wire *hdr,
				Timestamp *rcv_tstamp)
{
	void *buf;
	timestamp_wire *ts;

	buf = ppi->tx_ptp;
	ts = buf + 34;

	msg_hdr_prepare(ppi->tx_ptp, PPM_DELAY_RESP, PP_DELAY_RESP_LENGTH,
			msg_hdr_get_msg_seq_id(hdr), 3,
			DSPOR(ppi)->logMinDelayReqInterval);
	msg_hdr_reset_flags(ppi->tx_ptp);
	msg_hdr_set_cf(ppi->tx_ptp, msg_hdr_get_cf(hdr));

	/* Delay_resp message */
	timestamp_internal_to_wire(ts, rcv_tstamp);
	memcpy((buf + 44), msg_hdr_get_src_port_id_clock_id(hdr),
	       PP_CLOCK_IDENTITY_LENGTH);
	*(uint16_t *) (buf + 52) = msg_hdr_get_src_port_id_port_no(hdr);
}

/* Unpack delayReq message from in buffer of ppi to internal structure */
void msg_unpack_delay_req(void *buf, MsgDelayReq *delay_req)
{
	timestamp_wire *ts = buf + 34;

	timestamp_wire_to_internal(&delay_req->originTimestamp, ts);
}

/* Unpack PDelayReq message from in buffer of ppi to internal structure */
void msg_unpack_pdelay_req(void *buf, MsgPDelayReq * pdelay_req)
{
	timestamp_wire *ts = buf + 34;

	timestamp_wire_to_internal(&pdelay_req->originTimestamp, ts);
}

/* Unpack delayResp message from IN buffer of ppi to internal structure */
void msg_unpack_delay_resp(void *buf, MsgDelayResp *resp)
{
	timestamp_wire *ts = buf + 34;

	timestamp_wire_to_internal(&resp->receiveTimestamp, ts);
	memcpy(&resp->requestingPortIdentity.clockIdentity,
	       (buf + 44), PP_CLOCK_IDENTITY_LENGTH);
	resp->requestingPortIdentity.portNumber =
		htons(*(uint16_t *) (buf + 52));
}

/* Unpack PDelayResp message from IN buffer of ppi to internal structure */
void msg_unpack_pdelay_resp(void *buf, MsgPDelayResp * presp)
{
	timestamp_wire *ts = buf + 34;

	timestamp_wire_to_internal(&presp->requestReceiptTimestamp, ts);
	memcpy(&presp->requestingPortIdentity.clockIdentity,
	       (buf + 44), PP_CLOCK_IDENTITY_LENGTH);
	presp->requestingPortIdentity.portNumber =
	    htons(*(uint16_t *) (buf + 52));
}

const char const *pp_msg_names[16] = {
	[PPM_SYNC] =			"sync",
	[PPM_DELAY_REQ] =		"delay_req",
	[PPM_PDELAY_REQ] =		"pdelay_req",
	[PPM_PDELAY_RESP] =		"pdelay_resp",

	[PPM_FOLLOW_UP] =		"follow_up",
	[PPM_DELAY_RESP] =		"delay_resp",
	[PPM_PDELAY_RESP_FOLLOW_UP] =	"pdelay_resp_follow_up",
	[PPM_ANNOUNCE] =		"announce",
	[PPM_SIGNALING] =		"signaling",
	[PPM_MANAGEMENT] =		"management",
};

/* Pack and send on general multicast ip adress an Announce message */
int msg_issue_announce(struct pp_instance *ppi)
{
	int len = msg_pack_announce(ppi);

	return __send_and_log(ppi, len, PPM_ANNOUNCE, PP_NP_GEN);
}

/* Pack and send on event multicast ip adress a Sync message */
int msg_issue_sync_followup(struct pp_instance *ppi)
{
	Timestamp tstamp;
	TimeInternal now, *time_snt;
	int e;

	/* Send sync on the event channel with the "current" timestamp */
	ppi->t_ops->get(ppi, &now);
	from_TimeInternal(&now, &tstamp);
	msg_pack_sync(ppi, &tstamp);
	e = __send_and_log(ppi, PP_SYNC_LENGTH, PPM_SYNC, PP_NP_EVT);
	if (e) return e;

	/* Send followup on general channel with sent-stamp of sync */
	time_snt = &ppi->last_snt_time;
	add_TimeInternal(time_snt, time_snt,
			 &OPTS(ppi)->outbound_latency);
	from_TimeInternal(time_snt, &tstamp);
	msg_pack_follow_up(ppi, &tstamp);
	return __send_and_log(ppi, PP_FOLLOW_UP_LENGTH, PPM_FOLLOW_UP,
			      PP_NP_GEN);
}

/* Pack and send on general multicast ip address a FollowUp message */
int msg_issue_pdelay_resp_followup(struct pp_instance *ppi, TimeInternal * time)
{
	Timestamp prec_orig_tstamp;
	from_TimeInternal(time, &prec_orig_tstamp);

	msg_pack_pdelay_resp_follow_up(ppi, &ppi->received_ptp_header,
				       &prec_orig_tstamp);

	return __send_and_log(ppi, PP_PDELAY_RESP_FOLLOW_UP_LENGTH,
			      PPM_PDELAY_RESP_FOLLOW_UP, PP_NP_GEN);
}

/* Pack and send on event multicast ip adress a DelayReq message */
static int msg_issue_delay_req(struct pp_instance *ppi)
{
	Timestamp orig_tstamp;
	TimeInternal now;
	ppi->t_ops->get(ppi, &now);
	from_TimeInternal(&now, &orig_tstamp);

	msg_pack_delay_req(ppi, &orig_tstamp);

	return __send_and_log(ppi, PP_DELAY_REQ_LENGTH, PPM_DELAY_REQ,
			      PP_NP_EVT);
}

/* Pack and send on event multicast ip adress a PDelayReq message */
static int msg_issue_pdelay_req(struct pp_instance *ppi)
{
	Timestamp orig_tstamp;
	TimeInternal now;
	ppi->t_ops->get(ppi, &now);
	from_TimeInternal(&now, &orig_tstamp);

	msg_pack_pdelay_req(ppi, &orig_tstamp);

	return __send_and_log(ppi, PP_PDELAY_REQ_LENGTH, PPM_PDELAY_REQ,
			      PP_NP_EVT);
}

int msg_issue_request(struct pp_instance *ppi)
{
	if (ppi->glbs->delay_mech == PP_E2E_MECH)
		return msg_issue_delay_req(ppi);
	return msg_issue_pdelay_req(ppi);
}

/* Pack and send on event multicast ip adress a DelayResp message */
int msg_issue_delay_resp(struct pp_instance *ppi, TimeInternal *time)
{
	Timestamp rcv_tstamp;
	from_TimeInternal(time, &rcv_tstamp);

	msg_pack_delay_resp(ppi, &ppi->received_ptp_header, &rcv_tstamp);

	return __send_and_log(ppi, PP_DELAY_RESP_LENGTH, PPM_DELAY_RESP,
			      PP_NP_GEN);
}

/* Pack and send on event multicast ip adress a DelayResp message */
int msg_issue_pdelay_resp(struct pp_instance *ppi, TimeInternal * time)
{
	Timestamp rcv_tstamp;
	from_TimeInternal(time, &rcv_tstamp);

	msg_pack_pdelay_resp(ppi, &ppi->received_ptp_header, &rcv_tstamp);

	return __send_and_log(ppi, PP_PDELAY_RESP_LENGTH, PPM_PDELAY_RESP,
			      PP_NP_EVT);
}
