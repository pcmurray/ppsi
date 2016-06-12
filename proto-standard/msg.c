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
int msg_unpack_header(struct pp_instance *ppi, void *buf, int plen)
{
	MsgHeader *hdr = &ppi->received_ptp_header;

	hdr->transportSpecific = (*(Nibble *) (buf + 0)) >> 4;
	hdr->messageType = (*(Enumeration4 *) (buf + 0)) & 0x0F;
	hdr->versionPTP = (*(UInteger4 *) (buf + 1)) & 0x0F;

	/* force reserved bit to zero if not */
	hdr->messageLength = htons(*(UInteger16 *) (buf + 2));
	hdr->domainNumber = (*(UInteger8 *) (buf + 4));

	memcpy(hdr->flagField, (buf + 6), PP_FLAG_FIELD_LENGTH);

	memcpy(&hdr->correctionfield.msb, (buf + 8), 4);
	memcpy(&hdr->correctionfield.lsb, (buf + 12), 4);
	hdr->correctionfield.msb = htonl(hdr->correctionfield.msb);
	hdr->correctionfield.lsb = htonl(hdr->correctionfield.lsb);
	memcpy(&hdr->sourcePortIdentity.clockIdentity, (buf + 20),
	       PP_CLOCK_IDENTITY_LENGTH);
	hdr->sourcePortIdentity.portNumber =
		htons(*(UInteger16 *) (buf + 28));
	hdr->sequenceId = htons(*(UInteger16 *) (buf + 30));
	hdr->controlField = (*(UInteger8 *) (buf + 32));
	hdr->logMessageInterval = (*(Integer8 *) (buf + 33));

	/*
	 * 9.5.1:
	 * Only PTP messages where the domainNumber field of the PTP message
	 * header (see 13.3.2.5) is identical to the defaultDS.domainNumber
	 * shall be accepted for processing by the protocol.
	 */
	if (hdr->domainNumber != GDSDEF(GLBS(ppi))->domainNumber)
		return -1;

	/*
	 * Alternate masters (17.4) not supported
	 * 17.4.2, NOTE:
	 * A slave node that does not want to use information from alternate
	 * masters merely ignores all messages with alternateMasterFlag TRUE.
	 */
	if (hdr->flagField[0] & PP_ALTERNATE_MASTER_FLAG)
		return -1;

	/*
	 * If the message is from us, we should discard it.
	 * The best way to do that is comparing the mac address,
	 * but it's easier to check the clock identity (we refuse
	 * any port, not only the same port, as we can't sync with
	 * ourself even when we'll run in multi-port mode.
	 */
	if (!memcmp(&ppi->received_ptp_header.sourcePortIdentity.clockIdentity,
			&DSPOR(ppi)->portIdentity.clockIdentity,
		    PP_CLOCK_IDENTITY_LENGTH))
		return -1;

	/*
	 * This FLAG_FROM_CURRENT_PARENT must be killed. Meanwhile, say it's
	 * from current parent if we have no current parent, so the rest works
	 */
	if (!DSPAR(ppi)->parentPortIdentity.portNumber ||
	    (!memcmp(&DSPAR(ppi)->parentPortIdentity.clockIdentity,
			&hdr->sourcePortIdentity.clockIdentity,
			PP_CLOCK_IDENTITY_LENGTH) &&
			(DSPAR(ppi)->parentPortIdentity.portNumber ==
			 hdr->sourcePortIdentity.portNumber)))
		ppi->flags |= PPI_FLAG_FROM_CURRENT_PARENT;
	else
		ppi->flags &= ~PPI_FLAG_FROM_CURRENT_PARENT;
	return 0;
}

/* Pack header message into out buffer of ppi */
void msg_pack_header(struct pp_instance *ppi, void *buf)
{
	/* (spec annex D and F) */
	*(UInteger8 *) (buf + 0) = 0; /* message type changed later */
	*(UInteger4 *) (buf + 1) = DSPOR(ppi)->versionNumber;
	*(UInteger8 *) (buf + 4) = DSDEF(ppi)->domainNumber;

	*(UInteger8 *) (buf + 6) = PP_TWO_STEP_FLAG;

	memset((buf + 8), 0, 8);
	memcpy((buf + 20), &DSPOR(ppi)->portIdentity.clockIdentity,
	       PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *) (buf + 28) =
				htons(DSPOR(ppi)->portIdentity.portNumber);
	*(UInteger8 *) (buf + 33) = 0x7F;
	/* Default value(spec Table 24) */
}

/* Pack Sync message into out buffer of ppi */
static void msg_pack_sync(struct pp_instance *ppi, Timestamp *orig_tstamp)
{
	void *buf;

	buf = ppi->tx_ptp;

	/* changes in header */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x00;

	/* Table 19 */
	*(UInteger16 *) (buf + 2) = htons(PP_SYNC_LENGTH);
	ppi->sent_seq[PPM_SYNC]++;
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_SYNC]);
	*(UInteger8 *) (buf + 32) = 0x00;

	/* Table 23 */
	*(Integer8 *) (buf + 33) = DSPOR(ppi)->logSyncInterval;
	memset((buf + 8), 0, 8);

	/* Sync message */
	*(UInteger16 *) (buf + 34) = htons(orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(orig_tstamp->nanosecondsField);
}

/* Unpack Sync message from in buffer */
void msg_unpack_sync(void *buf, MsgSync *sync)
{
	sync->originTimestamp.secondsField.msb =
		htons(*(UInteger16 *) (buf + 34));
	sync->originTimestamp.secondsField.lsb =
		htonl(*(UInteger32 *) (buf + 36));
	sync->originTimestamp.nanosecondsField =
		htonl(*(UInteger32 *) (buf + 40));
}

/* Pack Announce message into out buffer of ppi */
static int msg_pack_announce(struct pp_instance *ppi)
{
	void *buf;

	buf = ppi->tx_ptp;
	/* changes in header */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x0B;
	/* Table 19 */
	*(UInteger16 *) (buf + 2) = htons(PP_ANNOUNCE_LENGTH);
	ppi->sent_seq[PPM_ANNOUNCE]++;
	/* Table 21, set cf to zero */
	memset(buf + 8, 0, 8);
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_ANNOUNCE]);
	*(UInteger8 *) (buf + 32) = 0x05;
	/* Table 23 */
	*(Integer8 *) (buf + 33) = DSPOR(ppi)->logAnnounceInterval;

	/* Announce message */
	memset((buf + 34), 0, 10);
	*(Integer16 *) (buf + 44) = htons(DSPRO(ppi)->currentUtcOffset);
	*(UInteger8 *) (buf + 47) = DSPAR(ppi)->grandmasterPriority1;
	*(UInteger8 *) (buf + 48) = DSPAR(ppi)->grandmasterClockQuality.clockClass;
	*(Enumeration8 *) (buf + 49) = DSPAR(ppi)->grandmasterClockQuality.clockAccuracy;
	*(UInteger16 *) (buf + 50) =
		htons(DSPAR(ppi)->grandmasterClockQuality.offsetScaledLogVariance);
	*(UInteger8 *) (buf + 52) = DSPAR(ppi)->grandmasterPriority2;
	memcpy((buf + 53), &DSPAR(ppi)->grandmasterIdentity,
	       PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *) (buf + 61) = htons(DSCUR(ppi)->stepsRemoved);
	*(Enumeration8 *) (buf + 63) = DSPRO(ppi)->timeSource;

	if (pp_hooks.pack_announce)
		return pp_hooks.pack_announce(ppi);
	return PP_ANNOUNCE_LENGTH;
}

/* Unpack Announce message from in buffer of ppi to internal structure */
void msg_unpack_announce(void *buf, MsgAnnounce *ann)
{
	ann->originTimestamp.secondsField.msb =
		htons(*(UInteger16 *) (buf + 34));
	ann->originTimestamp.secondsField.lsb =
		htonl(*(UInteger32 *) (buf + 36));
	ann->originTimestamp.nanosecondsField =
		htonl(*(UInteger32 *) (buf + 40));
	ann->currentUtcOffset = htons(*(UInteger16 *) (buf + 44));
	ann->grandmasterPriority1 = *(UInteger8 *) (buf + 47);
	ann->grandmasterClockQuality.clockClass =
		*(UInteger8 *) (buf + 48);
	ann->grandmasterClockQuality.clockAccuracy =
		*(Enumeration8 *) (buf + 49);
	ann->grandmasterClockQuality.offsetScaledLogVariance =
		htons(*(UInteger16 *) (buf + 50));
	ann->grandmasterPriority2 = *(UInteger8 *) (buf + 52);
	memcpy(&ann->grandmasterIdentity, (buf + 53),
	       PP_CLOCK_IDENTITY_LENGTH);
	ann->stepsRemoved = htons(*(UInteger16 *) (buf + 61));
	ann->timeSource = *(Enumeration8 *) (buf + 63);

	if (pp_hooks.unpack_announce)
		pp_hooks.unpack_announce(buf, ann);
}

/* Pack Follow Up message into out buffer of ppi*/
static void msg_pack_follow_up(struct pp_instance *ppi, Timestamp *prec_orig_tstamp)
{
	void *buf;

	buf = ppi->tx_ptp;

	/* changes in header */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x08;

	/* Table 19 */
	*(UInteger16 *) (buf + 2) = htons(PP_FOLLOW_UP_LENGTH);
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_SYNC]);

	/* sentSyncSequenceId has already been incremented in msg_issue_sync */
	*(UInteger8 *) (buf + 32) = 0x02;

	/* Table 23 */
	*(Integer8 *) (buf + 33) = DSPOR(ppi)->logSyncInterval;

	/* Follow Up message */
	*(UInteger16 *) (buf + 34) =
		htons(prec_orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) =
		htonl(prec_orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) =
		htonl(prec_orig_tstamp->nanosecondsField);
}

/* Pack PDelay Follow Up message into out buffer of ppi*/
void msg_pack_pdelay_resp_follow_up(struct pp_instance *ppi,
				    MsgHeader * hdr,
				    Timestamp * prec_orig_tstamp)
{
	void *buf;

	buf = ppi->tx_ptp;

	/* header */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x0A;

	*(UInteger16 *) (buf + 2) = htons(PP_PDELAY_RESP_LENGTH);
	*(UInteger8 *) (buf + 4) = hdr->domainNumber;
	/* copy the correction field, 11.4.3 c.3) */
	*(Integer32 *) (buf + 8) = htonl(hdr->correctionfield.msb);
	*(Integer32 *) (buf + 12) = htonl(hdr->correctionfield.lsb);

	*(UInteger16 *) (buf + 30) = htons(hdr->sequenceId);
	*(UInteger8 *) (buf + 32) = 0x05;	/* controlField */

	/* requestReceiptTimestamp */
	*(UInteger16 *) (buf + 34) = htons(prec_orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(prec_orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(prec_orig_tstamp->nanosecondsField);

	/* requestingPortIdentity */
	memcpy((buf + 44), &hdr->sourcePortIdentity.clockIdentity,
	       PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *) (buf + 52) = htons(hdr->sourcePortIdentity.portNumber);
}

/* Unpack FollowUp message from in buffer of ppi to internal structure */
void msg_unpack_follow_up(void *buf, MsgFollowUp *flwup)
{
	flwup->preciseOriginTimestamp.secondsField.msb =
		htons(*(UInteger16 *) (buf + 34));
	flwup->preciseOriginTimestamp.secondsField.lsb =
		htonl(*(UInteger32 *) (buf + 36));
	flwup->preciseOriginTimestamp.nanosecondsField =
		htonl(*(UInteger32 *) (buf + 40));
}

/* Unpack PDelayRespFollowUp message from in buffer of ppi to internal struct */
void msg_unpack_pdelay_resp_follow_up(void *buf,
				      MsgPDelayRespFollowUp * pdelay_resp_flwup)
{
	pdelay_resp_flwup->responseOriginTimestamp.secondsField.msb =
	    htons(*(UInteger16 *) (buf + 34));
	pdelay_resp_flwup->responseOriginTimestamp.secondsField.lsb =
	    htonl(*(UInteger32 *) (buf + 36));
	pdelay_resp_flwup->responseOriginTimestamp.nanosecondsField =
	    htonl(*(UInteger32 *) (buf + 40));
	memcpy(&pdelay_resp_flwup->requestingPortIdentity.clockIdentity,
	       (buf + 44), PP_CLOCK_IDENTITY_LENGTH);
	pdelay_resp_flwup->requestingPortIdentity.portNumber =
	    htons(*(UInteger16 *) (buf + 52));
}

/* pack DelayReq message into out buffer of ppi */
static void msg_pack_delay_req(struct pp_instance *ppi, Timestamp *orig_tstamp)
{
	void *buf;

	buf = ppi->tx_ptp;

	/* changes in header */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x01;

	/* Table 19 */
	*(UInteger16 *) (buf + 2) = htons(PP_DELAY_REQ_LENGTH);
	ppi->sent_seq[PPM_DELAY_REQ]++;
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_DELAY_REQ]);
	*(UInteger8 *) (buf + 32) = 0x01;

	/* Table 23 */
	*(Integer8 *) (buf + 33) = 0x7F;

	/* Table 24 */
	memset((buf + 8), 0, 8);

	/* Delay_req message */
	*(UInteger16 *) (buf + 34) = htons(orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(orig_tstamp->nanosecondsField);
}

/* pack DelayReq message into out buffer of ppi */
void msg_pack_pdelay_req(struct pp_instance *ppi, Timestamp * orig_tstamp)
{
	void *buf;

	buf = ppi->tx_ptp;

	/* changes in header 11.4.3 */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x02;

	*(UInteger16 *) (buf + 2) = htons(PP_PDELAY_REQ_LENGTH);
	ppi->sent_seq[PPM_DELAY_REQ]++;

	/* TO DO, 11.4.3 a.1) if synthed peer-to-peer TC */
	/* *(char *)(buf + 4) = 0 .- not sythonized / X synt domain */

	memset((buf + 8), 0, 8);
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_PDELAY_REQ]);
	*(UInteger8 *) (buf + 32) = 0x05;

	/* Table 23 */
	*(Integer8 *) (buf + 33) = 0x7F;

	/* PDelay_req message */
	*(UInteger16 *) (buf + 34) = htons(orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(orig_tstamp->nanosecondsField);
}

/* pack PDelayResp message into OUT buffer of ppi */
void msg_pack_pdelay_resp(struct pp_instance *ppi,
			  MsgHeader * hdr, Timestamp * rcv_tstamp)
{
	void *buf;

	buf = ppi->tx_ptp;

	/* header */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x03;

	*(UInteger16 *) (buf + 2) = htons(PP_PDELAY_RESP_LENGTH);
	*(UInteger8 *) (buf + 4) = hdr->domainNumber;
	/* set 0 the correction field, 11.4.3 c.3) */
	memset((buf + 8), 0, 8);

	*(UInteger16 *) (buf + 30) = htons(hdr->sequenceId);
	*(UInteger8 *) (buf + 32) = 0x05;	/* controlField */

	/* requestReceiptTimestamp */
	*(UInteger16 *) (buf + 34) = htons(rcv_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(rcv_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(rcv_tstamp->nanosecondsField);

	/* requestingPortIdentity */
	memcpy((buf + 44), &hdr->sourcePortIdentity.clockIdentity,
	       PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *) (buf + 52) = htons(hdr->sourcePortIdentity.portNumber);
}

/* pack DelayResp message into OUT buffer of ppi */
static void msg_pack_delay_resp(struct pp_instance *ppi,
			 MsgHeader *hdr, Timestamp *rcv_tstamp)
{
	void *buf;

	buf = ppi->tx_ptp;

	/* changes in header */
	*(char *)(buf + 0) = *(char *)(buf + 0) & 0xF0;
	/* RAZ messageType */
	*(char *)(buf + 0) = *(char *)(buf + 0) | 0x09;

	/* Table 19 */
	*(UInteger16 *) (buf + 2) = htons(PP_DELAY_RESP_LENGTH);
	*(UInteger8 *) (buf + 4) = hdr->domainNumber;
	memset((buf + 8), 0, 8);

	/* Copy correctionField of delayReqMessage */
	*(Integer32 *) (buf + 8) = htonl(hdr->correctionfield.msb);
	*(Integer32 *) (buf + 12) = htonl(hdr->correctionfield.lsb);

	*(UInteger16 *) (buf + 30) = htons(hdr->sequenceId);

	*(UInteger8 *) (buf + 32) = 0x03;

	/* Table 23 */
	*(Integer8 *) (buf + 33) = DSPOR(ppi)->logMinDelayReqInterval;

	/* Table 24 */

	/* Delay_resp message */
	*(UInteger16 *) (buf + 34) =
		htons(rcv_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(rcv_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(rcv_tstamp->nanosecondsField);
	memcpy((buf + 44), &hdr->sourcePortIdentity.clockIdentity,
		  PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *) (buf + 52) =
		htons(hdr->sourcePortIdentity.portNumber);
}

/* Unpack delayReq message from in buffer of ppi to internal structure */
void msg_unpack_delay_req(void *buf, MsgDelayReq *delay_req)
{
	delay_req->originTimestamp.secondsField.msb =
		htons(*(UInteger16 *) (buf + 34));
	delay_req->originTimestamp.secondsField.lsb =
		htonl(*(UInteger32 *) (buf + 36));
	delay_req->originTimestamp.nanosecondsField =
		htonl(*(UInteger32 *) (buf + 40));
}

/* Unpack PDelayReq message from in buffer of ppi to internal structure */
void msg_unpack_pdelay_req(void *buf, MsgPDelayReq * pdelay_req)
{
	pdelay_req->originTimestamp.secondsField.msb =
	    htons(*(UInteger16 *) (buf + 34));
	pdelay_req->originTimestamp.secondsField.lsb =
	    htonl(*(UInteger32 *) (buf + 36));
	pdelay_req->originTimestamp.nanosecondsField =
	    htonl(*(UInteger32 *) (buf + 40));
}

/* Unpack delayResp message from IN buffer of ppi to internal structure */
void msg_unpack_delay_resp(void *buf, MsgDelayResp *resp)
{
	resp->receiveTimestamp.secondsField.msb =
		htons(*(UInteger16 *) (buf + 34));
	resp->receiveTimestamp.secondsField.lsb =
		htonl(*(UInteger32 *) (buf + 36));
	resp->receiveTimestamp.nanosecondsField =
		htonl(*(UInteger32 *) (buf + 40));
	memcpy(&resp->requestingPortIdentity.clockIdentity,
	       (buf + 44), PP_CLOCK_IDENTITY_LENGTH);
	resp->requestingPortIdentity.portNumber =
		htons(*(UInteger16 *) (buf + 52));
}

/* Unpack PDelayResp message from IN buffer of ppi to internal structure */
void msg_unpack_pdelay_resp(void *buf, MsgPDelayResp * presp)
{
	presp->requestReceiptTimestamp.secondsField.msb =
	    htons(*(UInteger16 *) (buf + 34));
	presp->requestReceiptTimestamp.secondsField.lsb =
	    htonl(*(UInteger32 *) (buf + 36));
	presp->requestReceiptTimestamp.nanosecondsField =
	    htonl(*(UInteger32 *) (buf + 40));
	memcpy(&presp->requestingPortIdentity.clockIdentity,
	       (buf + 44), PP_CLOCK_IDENTITY_LENGTH);
	presp->requestingPortIdentity.portNumber =
	    htons(*(UInteger16 *) (buf + 52));
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
