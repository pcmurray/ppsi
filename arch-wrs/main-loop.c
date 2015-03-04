/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Alessandro Rubini
 *
 * Released to the public domain
 */

/*
 * This is the main loop for the wr-switch architecture. It's amost
 * the same as the unix main loop, but we must serve RPC calls too
 */
#include <stdlib.h>
#include <errno.h>
#include <sys/select.h>
#include <linux/if_ether.h>

#include <ppsi/ppsi.h>
#include <ppsi-wrs.h>
#include <wr-api.h>
#include <hal_exports.h>
extern struct minipc_pd __rpcdef_get_port_state;
extern struct minipc_pd __rpcdef_hdover_cmd;

/* Call pp_state_machine for each instance. To be called periodically,
 * when no packets are incoming */
static int run_all_state_machines(struct pp_globals *ppg)
{
	int j;
	int delay_ms = 0, delay_ms_j;

	for (j = 0; j < ppg->nlinks; j++) {
		struct pp_instance *ppi = INST(ppg, j);
		int old_lu = WR_DSPOR(ppi)->linkUP;
		hexp_port_state_t state;

		minipc_call(hal_ch, DEFAULT_TO, &__rpcdef_get_port_state,
			&state, ppi->iface_name);

		if ((state.valid) && (state.up))
			WR_DSPOR(ppi)->linkUP = 1;
		else
			WR_DSPOR(ppi)->linkUP = 0;

		if (old_lu != WR_DSPOR(ppi)->linkUP) {

			pp_diag(ppi, fsm, 1, "iface %s went %s\n",
				ppi->iface_name, WR_DSPOR(ppi)->linkUP ? "up":"down");

			if (WR_DSPOR(ppi)->linkUP) {
				ppi->state = PPS_INITIALIZING;
			}
			else {
				ppi->n_ops->exit(ppi);
				ppi->frgn_rec_num = 0;
				ppi->frgn_rec_best = -1;
				//ML: temp hack to make sure we don't reset servo when switchover
				//TODO: detect whether we have backup and reset only if we don't
// 				if (ppg->ebest_idx == ppi->port_idx)	
// 					wr_servo_reset();	
			}
		}

		/* Do not call state machine if link is down */
		if (WR_DSPOR(ppi)->linkUP)
			delay_ms_j = pp_state_machine(ppi, NULL, 0);
		else
			delay_ms_j = PP_DEFAULT_NEXT_DELAY_MS;

		/* delay_ms is the least delay_ms among all instances */
		if (j == 0)
			delay_ms = delay_ms_j;
		if (delay_ms_j < delay_ms)
			delay_ms = delay_ms_j;
	}
		//TODO: hooks
		if(WR_DSPOR(INST(ppg, 0))->ops->active_poll() < 0){
			pp_printf( "resetting primarySlavePortNumber \n");
			WR_DSCUR(INST(ppg, 0))->primarySlavePortNumber   = -1;
			WR_DSCUR(INST(ppg, 0))->primarySlavePortPriority = -1;
		}

	return delay_ms;
}

int wrs_holdover_check(struct pp_globals *ppg)
{
	hexp_holdover_state_t s;
	int cmd = HEXP_HDOVER_CMD_GET_STATE;
	struct DSDefault *def = ppg->defaultDS;
	struct DSParent  *par = ppg->parentDS;
	int old_ClockClass = def->clockQuality.clockClass;
	int ret =0;
	
	pp_printf( "wrs_holdover_check(): ");
	if( ret=minipc_call(hal_ch, DEFAULT_TO, &__rpcdef_hdover_cmd, &s, cmd, 0 /*value (unused)*/) < 0)
	{
		pp_printf( "minipc_call() bad, err:%d, errno: %d\n",ret, errno);
		return 0;
	}
	pp_printf( " holdover enabled=%d | state = %d | err %d | errno %d \n",s.enabled,s.state, ret, errno);
	if(!s.enabled || s.state == HEXP_HDOVER_INACTIVE) return 0;
	
	if(s.state == HEXP_HDOVER_ACTIVE) 
	{
		if(par->grandmasterClockQuality.clockClass == 6)  
		{
			def->clockQuality.clockClass            = 7;
			par->grandmasterClockQuality.clockClass = 7;
		}
		if(par->grandmasterClockQuality.clockClass == 13) 
		{
			def->clockQuality.clockClass            = 14;
			par->grandmasterClockQuality.clockClass = 14;
		}
	}
// 	if(s->state == HEXP_HDOVER_OUTSIDE_SPEC) 
// 	{
// 	       // degradation alternative A, see IEEE1588-2008, table 5,page 55
// 		if(def->clockQuality.clockClass == 7)  def->clockQuality.clockClass == 52;
// 		if(def->clockQuality.clockClass == 14) def->clockQuality.clockClass == 58;
// 	}
	ppg->classClass_update = (old_ClockClass!=def->clockQuality.clockClass);
	return ppg->classClass_update;
}

void wrs_main_loop(struct pp_globals *ppg)
{
	struct pp_instance *ppi;
	int delay_ms;
	int j;

	/* Initialize each link's state machine */
	for (j = 0; j < ppg->nlinks; j++) {

		ppi = INST(ppg, j);

		/*
		* If we are sending or receiving raw ethernet frames,
		* the ptp payload is one-eth-header bytes into the frame
		*/
		if (ppi->ethernet_mode)
			NP(ppi)->ptp_offset = ETH_HLEN;

		/*
		* The main loop here is based on select. While we are not
		* doing anything else but the protocol, this allows extra stuff
		* to fit.
		*/
		ppi->is_new_state = 1;
	}

	delay_ms = run_all_state_machines(ppg);
	ppg->classClass_update = 0;
	
	while (1) {
		int i;

		minipc_server_action(ppsi_ch, 10 /* ms */);
			
		/*
		 * If Ebest was changed in previous loop, run best
		 * master clock before checking for new packets, which
		 * would affect port state again
		 */
		if (ppg->ebest_updated || ppg->classClass_update) { 
			for (j = 0; j < ppg->nlinks; j++) {
				int new_state;
				struct pp_instance *ppi = INST(ppg, j);
				new_state = bmc(ppi);
				if (new_state != ppi->state) {
					ppi->state = new_state;
					ppi->is_new_state = 1;
				}
			}
			ppg->ebest_updated = 0;
			
			if(ppg->classClass_update){
				delay_ms = run_all_state_machines(ppg); //announces sent here
				ppg->classClass_update = 0;
			}
		}

		i = wrs_net_ops.check_packet(ppg, delay_ms);

		if (i < 0)
			continue;

		if (i == 0) {
		  		/*
			* check holdover. If any holdover-related ClockClass degradation is required,
			* the function will do it and return 1. In such case, 
			* 1. announce timeout is expired
			* 2 announce is sent (immediatelly)
			* 3. 
			*/
			if(wrs_holdover_check(ppg)){ 
				
				for (j = 0; j < ppg->nlinks; j++)
				{
					struct pp_instance *ppi = INST(ppg, j);
					/* force sending on all ports announce as soon as possible*/
					pp_timeout_set(ppi, PP_TO_ANN_INTERVAL, 0 /* now*/);
					
					/* forget old announces, this means that the BC will become GM
					* in the BMCA we run below. this will happen through m1()
					* and the defaultDS with updated clockClass will be copied
					*/
					ppi->frgn_rec_num = 0;
					continue;
				}

			}
			delay_ms = run_all_state_machines(ppg);
			continue;
		}
		//TODO: if works, make it sexy, now it's a hack
// 		if(WR_DSPOR(INST(ppg, 0))->ops->active_poll() < 0) // no active port
// 		{
// 			pp_printf("[Switchover] reset primarySlavePortNumber/Priority\n");
// 			DSCUR(INST(ppg, 0))->primarySlavePortNumber   = -1;
// 			DSCUR(INST(ppg, 0))->primarySlavePortPriority = -1; 
// 		}
		
		/* If delay_ms is -1, the above ops.check_packet will continue
		 * consuming the previous timeout (see its implementation).
		 * This ensures that every state machine is called at least once
		 * every delay_ms */
		delay_ms = -1;

		for (j = 0; j < ppg->nlinks; j++) {
			int tmp_d;
			ppi = INST(ppg, j);

			if ((NP(ppi)->ch[PP_NP_GEN].pkt_present) ||
			    (NP(ppi)->ch[PP_NP_EVT].pkt_present)) {

				i = ppi->n_ops->recv(ppi, ppi->rx_frame,
						PP_MAX_FRAME_LENGTH - 4,
						&ppi->last_rcv_time);

				if (i == -2) {
					continue; /* dropped */
				}
				if (i == -1) {
					pp_diag(ppi, frames, 1,
						"Receive Error %i: %s\n",
						errno, strerror(errno));
					continue;
				}
				if (i < PP_MINIMUM_LENGTH) {
					pp_diag(ppi, frames, 1,
						"Short frame: %d < %d\n", i,
						PP_MINIMUM_LENGTH);
					continue;
				}

				tmp_d = pp_state_machine(ppi, ppi->rx_ptp,
					i - NP(ppi)->ptp_offset);

				if ((delay_ms == -1) || (tmp_d < delay_ms))
					delay_ms = tmp_d;
			}
		}
	}
}
