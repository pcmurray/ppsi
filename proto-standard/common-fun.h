/*
 * Aurelio Colosimo for CERN, 2011 -- GNU LGPL v2.1 or later
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 */

#ifndef __COMMON_FUN_H
#define __COMMON_FUN_H

#include <ppsi/ppsi.h>
#include <ppsi/diag.h>

/* Contains all functions common to more than one state */

/* returns -1 in case of error, see below */
int st_com_execute_slave(struct pp_instance *ppi, int check_delayreq);

void st_com_restart_annrec_timer(struct pp_instance *ppi);

int st_com_check_record_update(struct pp_instance *ppi);

/* Each of the following "handle" functions" return 0 in case of correct
 * message, -1 in case the message contained in buf is not proper (e.g. size
 * is not the expected one
 */
int st_com_slave_handle_announce(struct pp_instance *ppi, unsigned char *buf,
				 int len);

int st_com_master_handle_announce(struct pp_instance *ppi, unsigned char *buf,
				  int len);

int st_com_slave_handle_sync(struct pp_instance *ppi, unsigned char *buf,
			     int len);

int st_com_master_handle_sync(struct pp_instance *ppi, unsigned char *buf,
			      int len);

int st_com_slave_handle_followup(struct pp_instance *ppi, unsigned char *buf,
				 int len);

static inline int __send_and_log(struct pp_instance *ppi, int msglen,
				 int msgtype, int chtype)
{
	if (pp_net_ops.send(ppi, ppi->buf_out, msglen,
			    &ppi->last_snt_time, chtype, 0) < msglen) {
		if (pp_verbose_frames)
			PP_PRINTF("%s(%d) Message can't be sent\n",
			pp_msg_names[msgtype], msgtype);
		return -1;
	}
	/* FIXME: verbose_frames should be looped back in the send method */
	if (pp_verbose_frames)
		PP_VPRINTF("SENT %02d %d.%09d %s\n", msglen,
			ppi->last_snt_time.seconds,
			ppi->last_snt_time.nanoseconds,
			pp_msg_names[msgtype]);
	ppi->sent_seq_id[msgtype]++; /* FIXME: fold in the send method too? */
	return 0;
}

#endif /* __COMMON_FUN_H */
