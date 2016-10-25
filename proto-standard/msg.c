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

/* Pack header into output buffer -- only called by state-initializing */
void msg_pack_header(struct pp_instance *ppi, void *buf)
{
	memset(buf, 0, 34);
	*(char *)(buf + 1) = DSPOR(ppi)->versionNumber;
	*(char *)(buf + 4) = DSDEF(ppi)->domainNumber;

	memcpy((buf + 20), &DSPOR(ppi)->portIdentity.clockIdentity,
	       PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *)(buf + 28) =
				htons(DSPOR(ppi)->portIdentity.portNumber);
}

/* Pack Sync message into out buffer of ppi */
static int msg_pack_sync(struct pp_instance *ppi, Timestamp *orig_tstamp)
{
	void *buf = ppi->tx_ptp;
	UInteger8 *flags8 = buf + 6;;
	int len = PP_SYNC_LENGTH;

	ppi->sent_seq[PPM_SYNC]++;
	/* Header */
	*(char *)(buf + 0) = PPM_SYNC;
	*(UInteger16 *) (buf + 2) = htons(len);
	flags8[0] = PP_TWO_STEP_FLAG; /* Table 20) */
	flags8[1] = 0;
	memset((buf + 8), 0, 8); /* Table 21: correction field */
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_SYNC]);
	*(UInteger8 *) (buf + 32) = 0x00; /* Table 23 */
	*(Integer8 *) (buf + 33) = DSPOR(ppi)->logSyncInterval; /* Table 24 */

	/* Sync message */
	*(UInteger16 *) (buf + 34) = htons(orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(orig_tstamp->nanosecondsField);
	return len;
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

/*
 * Setup flags for an announce message.
 * Set byte 1 of flags taking it from timepropertiesDS' flags field,
 * see 13.3.2.6, Table 20
 */
static void msg_set_announce_flags(UInteger8 *flags, struct pp_instance *ppi)
{
	struct DSTimeProperties *prop = DSPRO(ppi);
	const Boolean *ptrs[] = {
		&prop->leap61,
		&prop->leap59,
		&prop->currentUtcOffsetValid,
		&prop->ptpTimescale,
		&prop->timeTraceable,
		&prop->frequencyTraceable,
	};
	int i;

	/*
	 * alternate master always false, twoStepFlag false in announce,
	 * unicastFlag always false, other flags always false
	 */
	flags[0] = 0;
	for (flags[1] = 0, i = 0; i < ARRAY_SIZE(ptrs); i++)
		if (*ptrs[i])
			flags[1] |= (1 << i);
}

/* Pack Announce message into out buffer of ppi */
static int msg_pack_announce(struct pp_instance *ppi)
{
	void *buf = ppi->tx_ptp;
	UInteger8 *flags8 = buf + 6;;
	int len = PP_ANNOUNCE_LENGTH;

	ppi->sent_seq[PPM_ANNOUNCE]++;
	/* Header */
	*(char *)(buf + 0) = PPM_ANNOUNCE;
	*(UInteger16 *) (buf + 2) = htons(len);
	msg_set_announce_flags(flags8, ppi);
	memset((buf + 8), 0, 8); /* Table 21: correction field */
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_ANNOUNCE]);
	*(UInteger8 *)(buf + 32) = 0x05; /* Table 23 */
	*(Integer8 *)(buf + 33) = DSPOR(ppi)->logAnnounceInterval; /* Tab 24 */

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
		len = pp_hooks.pack_announce(ppi);
	return len;
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
static int msg_pack_follow_up(struct pp_instance *ppi, Timestamp *prec_orig_tstamp)
{
	void *buf = ppi->tx_ptp;
	UInteger16 *flags16 = buf + 6;
	int len = PP_FOLLOW_UP_LENGTH;

	/* Header */
	*(char *)(buf + 0) = PPM_FOLLOW_UP;
	*(UInteger16 *) (buf + 2) = htons(len);
	*flags16 = 0; /* may have aternateMasterFlag alone */
	memset((buf + 8), 0, 8); /* FIXME: correction field */
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_SYNC]);
	*(UInteger8 *) (buf + 32) = 0x02; /* Table 23 */
	*(Integer8 *) (buf + 33) = DSPOR(ppi)->logSyncInterval; /* Table 24 */

	/* Follow Up message */
	*(UInteger16 *) (buf + 34) =
		htons(prec_orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) =
		htonl(prec_orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) =
		htonl(prec_orig_tstamp->nanosecondsField);
	return len;
}

/* Pack PDelay Follow Up message into out buffer of ppi*/
static int msg_pack_pdelay_resp_follow_up(struct pp_instance *ppi,
					   MsgHeader * hdr,
					   Timestamp * prec_orig_tstamp)
{
	void *buf = ppi->tx_ptp;
	UInteger16 *flags16 = buf + 6;
	int len = PP_PDELAY_R_FUP_LENGTH;

	/* Header */
	*(char *)(buf + 0) = PPM_PDELAY_R_FUP;
	*(UInteger16 *) (buf + 2) = htons(len);
	*(UInteger8 *) (buf + 4) = hdr->domainNumber;
	*flags16 = 0;
	/* copy the correction field, 11.4.3 c.3) */
	*(Integer32 *) (buf + 8) = htonl(hdr->correctionfield.msb);
	*(Integer32 *) (buf + 12) = htonl(hdr->correctionfield.lsb);
	*(UInteger16 *) (buf + 30) = htons(hdr->sequenceId);
	*(UInteger8 *) (buf + 32) = 0x05; /* Table 23 */
	*(Integer8 *) (buf + 33) = 0x7f; /* Table 24 */

	/* requestReceiptTimestamp */
	*(UInteger16 *) (buf + 34) = htons(prec_orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(prec_orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(prec_orig_tstamp->nanosecondsField);

	/* requestingPortIdentity */
	memcpy((buf + 44), &hdr->sourcePortIdentity.clockIdentity,
	       PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *) (buf + 52) = htons(hdr->sourcePortIdentity.portNumber);
	return len;
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
static int msg_pack_delay_req(struct pp_instance *ppi, Timestamp *orig_tstamp)
{
	void *buf = ppi->tx_ptp;
	UInteger16 *flags16 = buf + 6;
	int len = PP_DELAY_REQ_LENGTH;

	ppi->sent_seq[PPM_DELAY_REQ]++;
	/* Header */
	*(char *)(buf + 0) = PPM_DELAY_REQ;
	*(UInteger16 *) (buf + 2) = htons(len);
	*flags16 = 0;
	memset((buf + 8), 0, 8); /* FIXME: correctionField */
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_DELAY_REQ]);
	*(UInteger8 *) (buf + 32) = 0x01; /* Table 23 */
	*(Integer8 *) (buf + 33) = 0x7F; /* Table 24 */

	/* Delay_req message */
	*(UInteger16 *) (buf + 34) = htons(orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(orig_tstamp->nanosecondsField);
	return len;
}

/* pack DelayReq message into out buffer of ppi */
static int msg_pack_pdelay_req(struct pp_instance *ppi,
				Timestamp * orig_tstamp)
{
	void *buf = ppi->tx_ptp;
	UInteger16 *flags16 = buf + 6;
	int len = PP_PDELAY_REQ_LENGTH;

	ppi->sent_seq[PPM_PDELAY_REQ]++;
	/* Header */
	*(char *)(buf + 0) = PPM_PDELAY_REQ;
	*(UInteger16 *) (buf + 2) = htons(len);
	*flags16 = 0;
	memset((buf + 8), 0, 8); /* FIXME: correctionField */
	*(UInteger16 *) (buf + 30) = htons(ppi->sent_seq[PPM_PDELAY_REQ]);
	*(UInteger8 *) (buf + 32) = 0x05; /* Table 23 */
	*(Integer8 *) (buf + 33) = 0x7F; /* Table 24 */

	/* PDelay_req message */
	*(UInteger16 *) (buf + 34) = htons(orig_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(orig_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(orig_tstamp->nanosecondsField);
	memset(buf + 44, 0, 10); /* reserved to match pdelay_resp length */
	return len;
}

/* pack PDelayResp message into OUT buffer of ppi */
static int msg_pack_pdelay_resp(struct pp_instance *ppi,
				MsgHeader * hdr, Timestamp * rcv_tstamp)
{
	void *buf = ppi->tx_ptp;
	UInteger8 *flags8 = buf + 6;;
	int len = PP_PDELAY_RESP_LENGTH;

	/* Header */
	*(char *)(buf + 0) = PPM_PDELAY_RESP;
	*(UInteger16 *) (buf + 2) = htons(len);
	flags8[0] = PP_TWO_STEP_FLAG; /* Table 20) */
	flags8[1] = 0;
	memset((buf + 8), 0, 8); /* FIXME: correctionField */
	*(UInteger16 *) (buf + 30) = htons(hdr->sequenceId);
	*(UInteger8 *) (buf + 32) = 0x05; /* Table 23 */
	*(Integer8 *) (buf + 33) = 0x7F; /* Table 24 */

	/* requestReceiptTimestamp */
	*(UInteger16 *) (buf + 34) = htons(rcv_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(rcv_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(rcv_tstamp->nanosecondsField);

	/* requestingPortIdentity */
	memcpy((buf + 44), &hdr->sourcePortIdentity.clockIdentity,
	       PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *) (buf + 52) = htons(hdr->sourcePortIdentity.portNumber);
	return len;
}

/* pack DelayResp message into OUT buffer of ppi */
static int msg_pack_delay_resp(struct pp_instance *ppi,
			 MsgHeader *hdr, Timestamp *rcv_tstamp)
{
	void *buf = ppi->tx_ptp;
	UInteger16 *flags16 = buf + 6;;
	int len = PP_DELAY_RESP_LENGTH;

	/* Header */
	*(char *)(buf + 0) = PPM_DELAY_RESP;
	*(UInteger16 *) (buf + 2) = htons(len);
	*flags16 = 0;
	/* Copy correctionField of delayReqMessage */
	*(Integer32 *) (buf + 8) = htonl(hdr->correctionfield.msb);
	*(Integer32 *) (buf + 12) = htonl(hdr->correctionfield.lsb);
	*(UInteger16 *) (buf + 30) = htons(hdr->sequenceId);
	*(UInteger8 *) (buf + 32) = 0x03; /* Table 23 */
	*(Integer8 *)(buf + 33) = DSPOR(ppi)->logMinDelayReqInterval; /* T 24 */

	/* Delay_resp message */
	*(UInteger16 *) (buf + 34) =
		htons(rcv_tstamp->secondsField.msb);
	*(UInteger32 *) (buf + 36) = htonl(rcv_tstamp->secondsField.lsb);
	*(UInteger32 *) (buf + 40) = htonl(rcv_tstamp->nanosecondsField);
	memcpy((buf + 44), &hdr->sourcePortIdentity.clockIdentity,
		  PP_CLOCK_IDENTITY_LENGTH);
	*(UInteger16 *) (buf + 52) =
		htons(hdr->sourcePortIdentity.portNumber);
	return len;
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
	[PPM_PDELAY_R_FUP] =		"pdelay_resp_follow_up",
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
	int e, len;

	/* Send sync on the event channel with the "current" timestamp */
	ppi->t_ops->get(ppi, &now);
	from_TimeInternal(&now, &tstamp);
	len = msg_pack_sync(ppi, &tstamp);
	e = __send_and_log(ppi, len, PPM_SYNC, PP_NP_EVT);
	if (e) return e;

	/* Send followup on general channel with sent-stamp of sync */
	time_snt = &ppi->last_snt_time;
	from_TimeInternal(time_snt, &tstamp);
	len = msg_pack_follow_up(ppi, &tstamp);
	return __send_and_log(ppi, len, PPM_FOLLOW_UP, PP_NP_GEN);
}

/* Pack and send on general multicast ip address a FollowUp message */
int msg_issue_pdelay_resp_followup(struct pp_instance *ppi, TimeInternal * time)
{
	Timestamp prec_orig_tstamp;
	int len;

	from_TimeInternal(time, &prec_orig_tstamp);

	len = msg_pack_pdelay_resp_follow_up(ppi, &ppi->received_ptp_header,
					     &prec_orig_tstamp);

	return __send_and_log(ppi, len, PPM_PDELAY_R_FUP, PP_NP_GEN);
}

/* Pack and send on event multicast ip adress a DelayReq message */
static int msg_issue_delay_req(struct pp_instance *ppi)
{
	Timestamp orig_tstamp;
	TimeInternal now;
	int len;

	ppi->t_ops->get(ppi, &now);
	from_TimeInternal(&now, &orig_tstamp);

	len = msg_pack_delay_req(ppi, &orig_tstamp);

	return __send_and_log(ppi, len, PPM_DELAY_REQ, PP_NP_EVT);
}

/* Pack and send on event multicast ip adress a PDelayReq message */
static int msg_issue_pdelay_req(struct pp_instance *ppi)
{
	Timestamp orig_tstamp;
	TimeInternal now;
	int len;

	ppi->t_ops->get(ppi, &now);
	from_TimeInternal(&now, &orig_tstamp);

	len = msg_pack_pdelay_req(ppi, &orig_tstamp);

	return __send_and_log(ppi, len, PPM_PDELAY_REQ, PP_NP_EVT);
}

int msg_issue_request(struct pp_instance *ppi)
{
	if (CONFIG_HAS_P2P && ppi->mech == PP_P2P_MECH)
		return msg_issue_pdelay_req(ppi);
	return msg_issue_delay_req(ppi);
}

/* Pack and send on event multicast ip adress a DelayResp message */
int msg_issue_delay_resp(struct pp_instance *ppi, TimeInternal *time)
{
	Timestamp rcv_tstamp;
	int len;

	from_TimeInternal(time, &rcv_tstamp);

	len = msg_pack_delay_resp(ppi, &ppi->received_ptp_header, &rcv_tstamp);

	return __send_and_log(ppi, len, PPM_DELAY_RESP, PP_NP_GEN);
}

/* Pack and send on event multicast ip adress a DelayResp message */
int msg_issue_pdelay_resp(struct pp_instance *ppi, TimeInternal * time)
{
	Timestamp rcv_tstamp;
	int len;

	from_TimeInternal(time, &rcv_tstamp);

	len = msg_pack_pdelay_resp(ppi, &ppi->received_ptp_header, &rcv_tstamp);

	return __send_and_log(ppi, len, PPM_PDELAY_RESP, PP_NP_EVT);
}
