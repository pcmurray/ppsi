/*
 * Aurelio Colosimo for CERN, 2011 -- GNU LGPL v2.1 or later
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 */

#include <pptp/pptp.h>

/* Contains all functions common to more than one state */

void st_com_execute_slave(struct pp_instance *ppi);

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
			     int len, TimeInternal *time);

int st_com_master_handle_sync(struct pp_instance *ppi, unsigned char *buf,
			      int len, TimeInternal *time);

int st_com_slave_handle_followup(struct pp_instance *ppi, unsigned char *buf,
				 int len);

int st_com_handle_pdelay_req(struct pp_instance *ppi, unsigned char *buf,
			     int len, TimeInternal *time);