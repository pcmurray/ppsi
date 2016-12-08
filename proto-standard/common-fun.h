/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#ifndef __COMMON_FUN_H
#define __COMMON_FUN_H

#include <ppsi/ppsi.h>

/* Contains all functions common to more than one state */

/* returns -1 in case of error, see below */
int st_com_execute_slave(struct pp_instance *ppi);

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

int st_com_peer_handle_preq(struct pp_instance *ppi, unsigned char *buf,
			    int len);

int st_com_peer_handle_pres(struct pp_instance *ppi, unsigned char *buf,
			    int len);

int st_com_peer_handle_pres_followup(struct pp_instance *ppi,
				     unsigned char *buf, int len);

int __send_and_log(struct pp_instance *ppi, int msglen, int chtype);

void correct_rx_timestamp(struct pp_instance *ppi, TimeInternal *t);
void correct_tx_timestamp(struct pp_instance *ppi, TimeInternal *t);
/* Count successfully received PTP packets */
static inline int __recv_and_count(struct pp_instance *ppi, void *pkt, int len,
		   TimeInternal *t)
{
	int ret;
	ret = ppi->n_ops->recv(ppi, pkt, len, t);
	if (ret > 0)
		ppi->ptp_rx_count++;
	correct_rx_timestamp(ppi, t);
	return ret;
}

#endif /* __COMMON_FUN_H */
