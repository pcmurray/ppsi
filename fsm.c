/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Alessandro Rubini
 *
 * Released to the public domain
 */
#include <ppsi/ppsi.h>

unsigned long pp_global_d_flags; /* This is the only "global" file in ppsi */

/*
 * This is somehow a duplicate of __pp_diag, but I still want
 * explicit timing in the fsm enter/stay/leave messages,
 * while there's no need to add times to all diagnostic messages
 */
static void pp_fsm_printf(struct pp_instance *ppi, char *fmt, ...)
{
	va_list args;
	TimeInternal t;
	unsigned long oflags = pp_global_d_flags;

	if (!pp_diag_allow(ppi, fsm, 1))
		return;

	/* temporarily set NOTIMELOG, as we'll print the time ourselves */
	pp_global_d_flags |= PP_FLAG_NOTIMELOG;
	ppi->t_ops->get(ppi, &t);
	pp_global_d_flags = oflags;

	pp_printf("diag-fsm-1-%s: %09d.%03d: ", ppi->port_name,
		  (int)t.seconds, (int)t.nanoseconds / 1000000);
	va_start(args, fmt);
	pp_vprintf(fmt, args);
	va_end(args);
}

/*
 * Diagnostics about state machine, enter, leave, remain
 */
enum {
	STATE_ENTER,
	STATE_LOOP,
	STATE_LEAVE
};

static void pp_diag_fsm(struct pp_instance *ppi, char *name, int sequence,
			int plen)
{
	if (sequence == STATE_ENTER) {
		/* enter with or without a packet len */
		pp_fsm_printf(ppi, "ENTER %s, packet len %i\n",
			  name, plen);
		return;
	}
	if (sequence == STATE_LOOP) {
		pp_fsm_printf(ppi, "%s: reenter in %i ms\n", name,
				ppi->next_delay);
		return;
	}
	/* leave has one \n more, so different states are separate */
	pp_fsm_printf(ppi, "LEAVE %s (next: %3i)\n\n",
		      name, ppi->next_state);
}

static struct pp_state_table_item *
get_current_state_table_item(struct pp_instance *ppi)
{
	struct pp_state_table_item *ip = ppi->current_state_item;;

	/* Avoid searching if we already know where we are */
	if (ip && ip->state == ppi->state)
		return ip;

	/* a linear search is affordable up to a few dozen items */
	for (ip = pp_state_table; ip->state != PPS_END_OF_TABLE; ip++)
		if (ip->state == ppi->state) {
			ppi->current_state_item = ip;
			return ip;
		}
	return NULL;
}

/*
 * Returns delay to next state, which is always zero.
 */
static int leave_current_state(struct pp_instance *ppi)
{
	ppi->state = ppi->next_state;
	ppi->is_new_state = 1;
	pp_timeout_setall(ppi);
	ppi->flags &= ~PPI_FLAGS_WAITING;
	pp_diag_fsm(ppi, ppi->current_state_item->name, STATE_LEAVE, 0);
	/* next_delay unused: go to new state now */
	return 0;
}

/*
 * Checks whether a packet has to be discarded and maybe updates port status
 * accordingly. Returns new packet length (0 if packet has to be discarded)
 */
static int pp_packet_prefilter(struct pp_instance *ppi)
{
	MsgHeader *hdr = &ppi->received_ptp_header;

	/*
	 * 9.5.1:
	 * Only PTP messages where the domainNumber field of the PTP message
	 * header (see 13.3.2.5) is identical to the defaultDS.domainNumber
	 * shall be accepted for processing by the protocol.
	 */
	if (hdr->domainNumber != GDSDEF(GLBS(ppi))->domainNumber) {
		pp_diag(ppi, frames, 1, "Wrong domain %i: discard\n",
			hdr->domainNumber);
		return -1;
	}

	/*
	 * Alternate masters (17.4) not supported
	 * 17.4.2, NOTE:
	 * A slave node that does not want to use information from alternate
	 * masters merely ignores all messages with alternateMasterFlag TRUE.
	 */
	if (hdr->flagField[0] & PP_ALTERNATE_MASTER_FLAG) {
		pp_diag(ppi, frames, 1, "Alternate master: discard\n");
		return -1;
	}

	/*
	 * If the message is from the same port that sent it, we should
	 * discard it (9.5.2.2)
	 */
	if (!memcmp(&ppi->received_ptp_header.sourcePortIdentity,
		    &DSPOR(ppi)->portIdentity,
		    sizeof(PortIdentity))) {
		pp_diag(ppi, frames, 1, "Looping frame: discard\n");
		return -1;
	}

	/*
	 * 9.5.2.3: if an announce message comes from another port of the same
	 * clock, switch all the ports but the lowest numbered one to
	 * PASSIVE. Since all involved ports will see each other's announce,
	 * we just switch __this__ instance's port's status to PASSIVE if we
	 * need to.
	 */
	if (hdr->messageType == PPM_ANNOUNCE &&
	    !memcmp(&hdr->sourcePortIdentity.clockIdentity,
		    &DSPOR(ppi)->portIdentity.clockIdentity,
		    sizeof(ClockIdentity))) {
		if (hdr->sourcePortIdentity.portNumber <
		    DSPOR(ppi)->portIdentity.portNumber)
			ppi->next_state = PPS_PASSIVE;
		return -1;
	}

	return 0;
}

/*
 * This is the state machine code. i.e. the extension-independent
 * function that runs the machine. Errors are managed and reported
 * here (based on the diag module). The returned value is the time
 * in milliseconds to wait before reentering the state machine.
 * the normal protocol. If an extended protocol is used, the table used
 * is that of the extension, otherwise the one in state-table-default.c
 */

int pp_state_machine(struct pp_instance *ppi, uint8_t *packet, int plen)
{
	struct pp_state_table_item *ip;
	int state, err = 0;
	int msgtype;

	if (plen) {
		msgtype = packet[0] & 0xf;
		pp_diag(ppi, frames, 1,
			"RECV %02d bytes at %d.%09d (type %x, %s)\n", plen,
			(int)ppi->last_rcv_time.seconds,
			(int)ppi->last_rcv_time.nanoseconds, msgtype,
			pp_msgtype_info[msgtype].name);
	}

	/*
	 * Discard too short packets
	 */
	if (plen < PP_HEADER_LENGTH) {
		plen = 0;
		packet = NULL;
	}

	/*
	 * Since all ptp frames have the same header, parse it now.
	 * In case of error continue without a frame, so the current
	 * ptp state can update ppi->next_delay and return a proper value
	 */
	if (plen && msg_unpack_header(ppi, packet, plen)) {
		packet = NULL;
		plen = 0;
	}

	state = ppi->state;

	ip = get_current_state_table_item(ppi);
	if (!ip) {
		pp_printf("fsm: Unknown state for port %s\n", ppi->port_name);
		return 10000; /* No way out. Repeat message every 10s */
	}

	/* found: handle this state */
	ppi->next_state = state;
	ppi->next_delay = 0;
	if (ppi->is_new_state)
		pp_diag_fsm(ppi, ip->name, STATE_ENTER, plen);
	/*
	 * Possibly filter out packet and maybe update port state
	 */
	if (packet) {
		err = pp_packet_prefilter(ppi);
		if (err < 0) {
			packet = NULL;
			plen = 0;
		}
	}
	if (ppi->state != ppi->next_state)
		return leave_current_state(ppi);

	err = ip->f1(ppi, packet, plen);
	if (err)
		pp_printf("fsm for %s: Error %i in %s\n",
			  ppi->port_name, err, ip->name);

	/* done: if new state mark it, and enter it now (0 ms) */
	if (ppi->state != ppi->next_state)
		return leave_current_state(ppi);

	ppi->is_new_state = 0;
	pp_diag_fsm(ppi, ip->name, STATE_LOOP, 0);
	return ppi->next_delay;
}
