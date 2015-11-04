#include <ppsi/ppsi.h>
#include "wr-api.h"

#define WR_SYNC_NSEC 1
#define WR_SYNC_TAI 2
#define WR_SYNC_PHASE 3
#define WR_TRACK_PHASE 4
#define WR_WAIT_SYNC_IDLE 5
#define WR_WAIT_OFFSET_STABLE 6

#define WR_SERVO_OFFSET_STABILITY_THRESHOLD 60 /* psec */

#define FIX_ALPHA_FRACBITS 40

const char *servo_state_str[] = {
	[WR_SYNC_NSEC] = "SYNC_NSEC",
	[WR_SYNC_TAI] = "SYNC_SEC",
	[WR_SYNC_PHASE] = "SYNC_PHASE",
	[WR_TRACK_PHASE] = "TRACK_PHASE",
	[WR_WAIT_SYNC_IDLE] = "SYNC_IDLE",
	[WR_WAIT_OFFSET_STABLE] = "OFFSET_STABLE",
};

int servo_state_valid = 0; /* FIXME: why? */
ptpdexp_sync_state_t cur_servo_state; /* FIXME: why? ML: beware when touching, now I use it as well.. */

static int tracking_enabled = 1; /* FIXME: why? */

void wr_servo_enable_tracking(int enable)
{
	tracking_enabled = enable;
}

/* my own timestamp arithmetic functions */

static void dump_timestamp(struct pp_instance *ppi, char *what, TimeInternal ts)
{
	pp_diag(ppi, servo, 2, "%s = %d:%d:%d\n", what, (int32_t)ts.seconds,
		  ts.nanoseconds, ts.phase);
}

static int64_t ts_to_picos(TimeInternal ts)
{
	return ts.seconds * 1000000000000LL
		+ ts.nanoseconds * 1000LL
		+ ts.phase;
}

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

static TimeInternal ts_add(TimeInternal a, TimeInternal b)
{
	TimeInternal c;

	c.phase = a.phase + b.phase;
	c.nanoseconds = a.nanoseconds + b.nanoseconds;
	c.seconds = a.seconds + b.seconds;

	while (c.phase >= 1000) {
		c.phase -= 1000;
		c.nanoseconds++;
	}
	while (c.nanoseconds >= PP_NSEC_PER_SEC) {
		c.nanoseconds -= PP_NSEC_PER_SEC;
		c.seconds++;
	}
	return c;
}

static TimeInternal ts_sub(TimeInternal a, TimeInternal b)
{
	TimeInternal c;

	c.phase = a.phase - b.phase;
	c.nanoseconds = a.nanoseconds - b.nanoseconds;
	c.seconds = a.seconds - b.seconds;

	while(c.phase < 0) {
		c.phase += 1000;
		c.nanoseconds--;
	}
	while(c.nanoseconds < 0) {
		c.nanoseconds += PP_NSEC_PER_SEC;
		c.seconds--;
	}
	return c;
}

/* "Hardwarizes" the timestamp - e.g. makes the nanosecond field a multiple
 * of 8ns cycles and puts the extra nanoseconds in the phase field */
static TimeInternal ts_hardwarize(TimeInternal ts, int clock_period_ps)
{
	int32_t q_threshold;

	q_threshold = (clock_period_ps + 999) / 1000;

	if (ts.nanoseconds > 0) {
		int32_t extra_nsec = ts.nanoseconds % q_threshold;

		if(extra_nsec) {
			ts.nanoseconds -= extra_nsec;
			ts.phase += extra_nsec * 1000;
		}
	}

	if (ts.nanoseconds < 0) {
		ts.nanoseconds += PP_NSEC_PER_SEC;
		ts.seconds--;
	}

	if (ts.seconds == -1 && ts.nanoseconds > 0) {
		ts.seconds++;
		ts.nanoseconds -= PP_NSEC_PER_SEC;
	}

	if (ts.nanoseconds < 0 && ts.nanoseconds >= (-q_threshold)
	    && ts.seconds == 0) {
		ts.nanoseconds += q_threshold;
		ts.phase -= q_threshold * 1000;
	}

	return ts;
}

/* end my own timestamp arithmetic functions */

static int got_sync[18] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0}; //FIXME: this f*sucks 

void wr_servo_reset()
{
	cur_servo_state.valid = 0;
	servo_state_valid = 0;
}

int wr_servo_init(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	struct wr_servo_state_t *s =
			&((struct wr_data_t *)ppi->ext_data)->servo_state[ppi->port_idx];
	int active_port = wrp->ops->active_poll();
	/* Determine the alpha coefficient */
	if (wrp->ops->read_calib_data(ppi, 0, 0,
		&s->fiber_fix_alpha, &s->clock_period_ps) != WR_HW_CALIB_OK)
		return -1;

	wrp->ops->enable_timing_output(ppi, 0);

	/* FIXME useful?
	strncpy(s->if_name, clock->netPath.ifaceName, 16);
	*/

	s->state = WR_SYNC_TAI;
	s->cur_setpoint = 0;
	s->missed_iters = 0;

	s->delta_tx_m = ((((int32_t)WR_DSPOR(ppi)->otherNodeDeltaTx.scaledPicoseconds.lsb) >> 16) & 0xffff) | (((int32_t)WR_DSPOR(ppi)->otherNodeDeltaTx.scaledPicoseconds.msb) << 16);
	s->delta_rx_m = ((((int32_t)WR_DSPOR(ppi)->otherNodeDeltaRx.scaledPicoseconds.lsb) >> 16) & 0xffff) | (((int32_t)WR_DSPOR(ppi)->otherNodeDeltaRx.scaledPicoseconds.msb) << 16);

	s->delta_tx_s = ((((int32_t)WR_DSPOR(ppi)->deltaTx.scaledPicoseconds.lsb) >> 16) & 0xffff) | (((int32_t)WR_DSPOR(ppi)->deltaTx.scaledPicoseconds.msb) << 16);
	s->delta_rx_s = ((((int32_t)WR_DSPOR(ppi)->deltaRx.scaledPicoseconds.lsb) >> 16) & 0xffff) | (((int32_t)WR_DSPOR(ppi)->deltaRx.scaledPicoseconds.msb) << 16);

	if(ppi->port_idx != wrp->ops->active_poll())
		return 0;
	cur_servo_state.delta_tx_m = (int64_t)s->delta_tx_m;
	cur_servo_state.delta_rx_m = (int64_t)s->delta_rx_m;
	cur_servo_state.delta_tx_s = (int64_t)s->delta_tx_s;
	cur_servo_state.delta_rx_s = (int64_t)s->delta_rx_s;

	/* FIXME: useful?
	strncpy(cur_servo_state.sync_source,
			  clock->netPath.ifaceName, 16);//fixme
	*/

	strcpy(cur_servo_state.slave_servo_state, "Uninitialized");

	servo_state_valid = 1;
	cur_servo_state.valid = 1;
	cur_servo_state.update_count = 0;
// 	if(active_port <-1)
	{
		cur_servo_state.active_port = active_port;
		pp_diag(ppi, servo, 1, "init active port: %d\n",active_port, ppi->port_idx);
	}

	got_sync[ppi->port_idx] = 0;
	return 0;
}

static int ph_adjust = 0;

int wr_servo_man_adjust_phase(int phase)
{
	ph_adjust = phase;
	return ph_adjust;
}

int wr_servo_got_sync(struct pp_instance *ppi, TimeInternal *t1,
		      TimeInternal *t2)
{
	struct wr_servo_state_t *s =
			&((struct wr_data_t *)ppi->ext_data)->servo_state[ppi->port_idx];

	s->t1 = *t1;
	s->t1.correct = 1;
	s->t2 = *t2;

	got_sync[ppi->port_idx] = 1;

	return 0;
}

int wr_servo_got_delay(struct pp_instance *ppi, Integer32 cf)
{
	struct wr_servo_state_t *s =
			&((struct wr_data_t *)ppi->ext_data)->servo_state[ppi->port_idx];

	s->t3 = ppi->t3;
	/*  s->t3.phase = 0; */
	s->t4 = ppi->t4;
	s->t4.correct = 1; /* clock->delay_req_receive_time.correct; */
	s->t4.phase = (int64_t) cf * 1000LL / 65536LL;

	if (GLBS(ppi)->delay_mech) {
		s->t5 = ppi->t5;
		s->t5.correct = 1;
		s->t5.phase = 0;
		s->t6 = ppi->t6;
		s->t6.phase = (int64_t) ppi->t6_cf * 1000LL / 65536LL;

		//wr_p2p_delay(ppi, s); /* already included in wr_servo_update() */
	}

	return 0;
}

int wr_p2p_delay(struct pp_instance *ppi, struct wr_servo_state_t *s)
{
	//uint64_t big_delta_fix;
	//static int errcount;

	//if(!s->t3.correct || !s->t4.correct ||
	   //!s->t5.correct || !s->t6.correct) {
		//errcount++;
		//if (errcount > 5) /* a 2-3 in a row are expected */
			//pp_error("%s: TimestampsIncorrect: %d %d %d %d\n",
				 //__func__, s->t3.correct, s->t4.correct,
				 //s->t5.correct, s->t6.correct);
		//return 0;
	//}
	//errcount = 0;

	//cur_servo_state.update_count++;

	//if (__PP_DIAG_ALLOW_FLAGS(pp_global_flags, pp_dt_servo, 1)) {
		//dump_timestamp(ppi, "servo:t1", s->t1);
		//dump_timestamp(ppi, "servo:t2", s->t2);
		//dump_timestamp(ppi, "servo:t3", s->t3);
		//dump_timestamp(ppi, "servo:t4", s->t4);
		//dump_timestamp(ppi, "servo:t5", s->t5);
		//dump_timestamp(ppi, "servo:t6", s->t6);
		//dump_timestamp(ppi, "->mdelay", s->mu);
	//}

	//s->mu = ts_sub(ts_sub(s->t6, s->t3), ts_sub(s->t5, s->t4));

	//big_delta_fix =  s->delta_tx_m + s->delta_tx_s
		       //+ s->delta_rx_m + s->delta_rx_s;

	//s->delta_ms = (((int64_t)(ts_to_picos(s->mu) - big_delta_fix) * (int64_t) s->fiber_fix_alpha) >> FIX_ALPHA_FRACBITS)
		//+ ((ts_to_picos(s->mu) - big_delta_fix) >> 1)
		//+ s->delta_tx_m + s->delta_rx_s + ph_adjust;

	return 1;
}

int wr_p2p_offset(struct pp_instance *ppi,
			struct wr_servo_state_t *s, TimeInternal *ts_offset_hw)

{
	//TimeInternal ts_offset;
	//static int errcount;

	//if(!s->t1.correct || !s->t2.correct) {
		//errcount++;
		//if (errcount > 5) /* a 2-3 in a row are expected */
			//pp_error("%s: TimestampsIncorrect: %d %d \n",
				 //__func__, s->t1.correct, s->t2.correct);
		//return 0;
	//}
	//errcount = 0;
	//got_sync[ppi->port_idx] = 0;

	//cur_servo_state.update_count++;

	//ts_offset = ts_add(ts_sub(s->t1, s->t2), picos_to_ts(s->delta_ms));
	//*ts_offset_hw = ts_hardwarize(ts_offset, s->clock_period_ps);

	//cur_servo_state.mu = (uint64_t)ts_to_picos(s->mu);
	//cur_servo_state.cur_offset = ts_to_picos(ts_offset);

	//cur_servo_state.delay_ms = s->delta_ms;
	//cur_servo_state.total_asymmetry =
		//(cur_servo_state.mu - 2LL * (int64_t)s->delta_ms);
	//cur_servo_state.fiber_asymmetry =
		//cur_servo_state.total_asymmetry
		//- (s->delta_tx_m + s->delta_rx_s)
		//+ (s->delta_rx_m + s->delta_tx_s);

	//cur_servo_state.tracking_enabled = tracking_enabled;

	return 1;

}


/********************************************************************************************
 * FIXME: This is bad, I know. First make it work, then make it beautiful...
********************************************************************************************/
typedef struct
{
	uint64_t start_tics;
	uint64_t timeout;
} timeout_t ;

struct wrs_socket {
	/* parameters for linearization of RX timestamps */
	uint32_t clock_period;
	uint32_t phase_transition;
	uint32_t dmtd_phase;
	int dmtd_phase_valid;
	timeout_t dmtd_update_tmo;
};
/*******************************************************************************************/


int wr_servo_update(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	struct wr_servo_state_t *s =
			&((struct wr_data_t *)ppi->ext_data)->servo_state[ppi->port_idx];
	struct wrs_socket *ss;
	uint64_t tics;
	uint64_t big_delta_fix;
	uint64_t delay_ms_fix;
	static int errcount;
	int32_t tmp_setpoint;
	int active_port = wrp->ops->active_poll();
	pp_diag(ppi, servo, 1, "active_poll = %d, active =%d port_idx = %d\n",
		active_port, cur_servo_state.active_port, ppi->port_idx);
	TimeInternal ts_offset, ts_offset_hw /*, ts_phase_adjust */;
	pp_diag(ppi, servo, 1, "in wr servo update, got_sync=%d\n",got_sync[ppi->port_idx]);
	
	if(!got_sync[ppi->port_idx])
		return 0;

	errcount = 0;
	if(!s->t1.correct || !s->t2.correct ||
	   !s->t3.correct || !s->t4.correct ||
	   !s->t5.correct || !s->t6.correct) {
		pp_diag(ppi, servo, 1, "timestamp incorrect: %d %d %d %d %d %d\n",
		s->t1.correct, s->t2.correct, s->t3.correct, s->t4.correct, 
		s->t5.correct, s->t6.correct);
		errcount++;
		if (errcount > 5) /* a 2-3 in a row are expected */
			pp_error("%s: TimestampsIncorrect: %d %d %d %d %d %d\n",
				 __func__, s->t1.correct, s->t2.correct,
				 s->t3.correct, s->t4.correct,
				 s->t5.correct, s->t6.correct);
		return 0; /* GUTI HACK: removing it, sometimes fixes some stuff 
					swithover related to mErr & bErr*/
	}
	errcount = 0;

	if(ppi->port_idx == cur_servo_state.active_port) {// only for active slave
		cur_servo_state.update_count++;
		GLBS(ppi)->active_backup_port = cur_servo_state.active_port;
	}

	got_sync[ppi->port_idx] = 0;

	//s->mu = ts_sub(ts_sub(s->t4, s->t1), ts_sub(s->t3, s->t2));
	s->mu = ts_sub(ts_sub(s->t6, s->t3), ts_sub(s->t5, s->t4)); /* pdelay */
	
	if(0) {
		pp_printf("pdelay t1 %d:%d:%d\n", s->t1);
		pp_printf("pdelay t2 %d:%d:%d\n", s->t2);
		pp_printf("pdelay t3 %d:%d:%d\n", s->t3);
		pp_printf("pdelay t4 %d:%d:%d\n", s->t4);
		pp_printf("pdelay t5 %d:%d:%d\n", s->t5);
		pp_printf("pdelay t6 %d:%d:%d\n", s->t6);
	}

	big_delta_fix =  s->delta_tx_m + s->delta_tx_s
		       + s->delta_rx_m + s->delta_rx_s;

	delay_ms_fix = (((int64_t)(ts_to_picos(s->mu) - big_delta_fix) * (int64_t) s->fiber_fix_alpha) >> FIX_ALPHA_FRACBITS)
		+ ((ts_to_picos(s->mu) - big_delta_fix) >> 1)
		+ s->delta_tx_m + s->delta_rx_s + ph_adjust;
		
	ppi->link_delay = (int64_t)((delay_ms_fix));
	
	if (__PP_DIAG_ALLOW_FLAGS(pp_global_flags, pp_dt_servo, 1)) {
		dump_timestamp(ppi, "servo:t1", s->t1);
		dump_timestamp(ppi, "servo:t2", s->t2);
		dump_timestamp(ppi, "servo:t3", s->t3);
		dump_timestamp(ppi, "servo:t4", s->t4);
		dump_timestamp(ppi, "->mdelay", s->mu);
		dump_timestamp(ppi, "->delay_ms_fix",  picos_to_ts(delay_ms_fix));
	}
	
	ts_offset = ts_add(ts_sub(s->t1, s->t2), ts_add(ppi->p2p_cField, picos_to_ts(delay_ms_fix)));
	//if((ts_offset.seconds<0) || (ts_offset.nanoseconds == 999999999)) {
		//pp_printf("ts_offset %d:%d\n", ts_offset.seconds, ts_offset.nanoseconds );
		//pp_printf("it happens\n");
		//return -1;
	//}
	ts_offset_hw = ts_hardwarize(ts_offset, s->clock_period_ps);
	pp_diag(ppi, servo, 1, "offset: %d [hw:%d]\n", 
	                   (ts_offset.phase    + ts_offset.nanoseconds * 1000),
	                   (ts_offset_hw.phase + ts_offset_hw.nanoseconds * 1000));
	pp_diag(ppi, servo, 2, "ts_hardwarize (before):  %d [s] %d [ns] %d [ps] \n", 
                           ts_offset.seconds, ts_offset.nanoseconds, ts_offset.phase);
	pp_diag(ppi, servo, 2, "ts_hardwarize (after):  %d [s] %d [ns] %d [ps] \n", 
                           ts_offset_hw.seconds, ts_offset_hw.nanoseconds, ts_offset_hw.phase);	
	pp_diag(ppi, servo, 2, "->mu=%d | big_delta_fix: %d | delay_ms_fix: %d\n",
                           s->mu,big_delta_fix, delay_ms_fix);

	if(ppi->port_idx == cur_servo_state.active_port) // only for active slave
	{
		cur_servo_state.mu = (uint64_t)ts_to_picos(s->mu);
		cur_servo_state.cur_offset = ts_to_picos(ts_offset);

		cur_servo_state.delay_ms = delay_ms_fix;
		cur_servo_state.total_asymmetry =
			(cur_servo_state.mu - 2LL * (int64_t)delay_ms_fix);
		cur_servo_state.fiber_asymmetry =
			cur_servo_state.total_asymmetry
			- (s->delta_tx_m + s->delta_rx_s)
			+ (s->delta_rx_m + s->delta_tx_s);

		cur_servo_state.tracking_enabled = tracking_enabled;
	}
	
	s->delta_ms = delay_ms_fix;

	tics = ppi->t_ops->calc_timeout(ppi, 0);

	if (wrp->ops->locking_poll(ppi, 0) != WR_SPLL_READY) {
		pp_diag(ppi, servo, 1, "PLL OutOfLock, should restart sync\n");
		wrp->ops->enable_timing_output(ppi, 0);
		/* TODO check
		 * DSPOR(ppi)->doRestart = TRUE; */
	}

	pp_diag(ppi, servo, 1, "wr_servo state: %s\n",
		cur_servo_state.slave_servo_state);

	switch (s->state) {
	case WR_WAIT_SYNC_IDLE:
		pp_diag(ppi, servo, 1, " WR_WAIT_SYNC_IDLE\n");
		if (!wrp->ops->adjust_in_progress()) {
			s->state = s->next_state;
		} else {
			pp_diag(ppi, servo, 1, "servo:busy\n");
		}
		break;

	case WR_SYNC_TAI:
		pp_diag(ppi, servo, 1, " WR_SYNC_TAI\n");
		wrp->ops->enable_timing_output(ppi, 0);
		
		//updated active port
		cur_servo_state.active_port = active_port;
		
		if (ts_offset_hw.seconds != 0) {
			pp_diag(ppi, servo, 1, " WR_SYNC_TAI-> counters touching at seconds\n");
			if(ppi->port_idx == cur_servo_state.active_port) // only for active slave
				strcpy(cur_servo_state.slave_servo_state, "SYNC_SEC");
			wrp->ops->adjust_counters(ts_offset_hw.seconds, 0);
			wrp->ops->adjust_phase(0, ppi->port_idx);

			s->next_state = WR_SYNC_NSEC;
			s->state = WR_WAIT_SYNC_IDLE;
			s->last_tics = tics;

		} else {
			s->state = WR_SYNC_NSEC;
		}
		break;

	case WR_SYNC_NSEC:
		pp_diag(ppi, servo, 1, " WR_SYNC_NSEC\n");
		if(ppi->port_idx == cur_servo_state.active_port) // only for active slave
			strcpy(cur_servo_state.slave_servo_state, "SYNC_NSEC");

		if (ts_offset_hw.nanoseconds != 0) {
			pp_diag(ppi, servo, 1, " WR_SYNC_NSEC-> counters touching at "
			"nanoseconds\n");
			wrp->ops->adjust_counters(0, ts_offset_hw.nanoseconds);

			s->next_state = WR_SYNC_NSEC;
			s->state = WR_WAIT_SYNC_IDLE;
			s->last_tics = tics;

		} else {
			s->state = WR_SYNC_PHASE;
			s->cur_setpoint = 0;
		}
		break;

	case WR_SYNC_PHASE:
		pp_diag(ppi, servo, 1, " WR_SYNC_PHASE\n");
		if(ppi->port_idx == cur_servo_state.active_port) // only for active slave
			strcpy(cur_servo_state.slave_servo_state, "SYNC_PHASE");
		if(ppi->port_idx == cur_servo_state.active_port) // only for active slave
		{
			s->cur_setpoint += ts_offset_hw.phase
			      + ts_offset_hw.nanoseconds * 1000; //ML: there should be no ns ??
		}
		else
		{
			/*
			 * On a backup port, setting the setpoint to the value already set by
			 * active port. 
			 * 
			 * The dmtd_phase is surely valid, otherwise the timestampp would be
			 * incorrect and we would have returned at the beginning of this fuction
			 * 
			 * NOTE: it might happen after switchover that a formely backup and 
			 *       now active port enters here, since the priority is not 
			 *       changed at switchover. This will be fixed in a separate/next
			 *       commit (independently useful).
			 * 
			 * FIXME: Sure, this is nasty, if works, will make it beautiful
			 */
			ss = (struct wrs_socket*)NP(ppi)->ch[PP_NP_GEN].arch_data;
			s->cur_setpoint = ss->dmtd_phase;
		}

		wrp->ops->adjust_phase(s->cur_setpoint, ppi->port_idx);

		s->next_state = WR_WAIT_OFFSET_STABLE;
		s->state = WR_WAIT_SYNC_IDLE;
		s->last_tics = tics;
		s->delta_ms_prev = s->delta_ms;
		break;

	case WR_WAIT_OFFSET_STABLE:
	{
		int64_t remaining_offset = abs(ts_to_picos(ts_offset_hw));

		pp_diag(ppi, servo, 1, " WR_WAIT_OFFSET_STABLE (offset=%d)\n",(int)remaining_offset);
		if (ts_offset_hw.seconds !=0 || ts_offset_hw.nanoseconds != 0)
			s->state = WR_SYNC_TAI;
		else
			if(remaining_offset < WR_SERVO_OFFSET_STABILITY_THRESHOLD || 
				ppi->port_idx != cur_servo_state.active_port) 
			{
				wrp->ops->enable_timing_output(ppi, 1);
				s->state = WR_TRACK_PHASE;
				s->missed_iters = 0;
			} else {
				s->missed_iters++;
			}

		if (s->missed_iters >= 10)
			s->state = WR_SYNC_TAI;
		break;
	}

	case WR_TRACK_PHASE:
		pp_diag(ppi, servo, 1, "WR_TRACK_PHASE \n");
		if(ppi->port_idx == cur_servo_state.active_port) // only for active slave
		{
			strcpy(cur_servo_state.slave_servo_state, "TRACK_PHASE");
			cur_servo_state.cur_setpoint = s->cur_setpoint;
			cur_servo_state.cur_skew = s->delta_ms - s->delta_ms_prev;
		}

		if (ts_offset_hw.seconds !=0 || ts_offset_hw.nanoseconds != 0)
				s->state = WR_SYNC_TAI;
		
		if(tracking_enabled) {
			// just follow the changes of deltaMS
			int32_t offset_change = (s->delta_ms - s->delta_ms_prev);
			int ret = 0;
			
			pp_diag(ppi, servo, 1, "in servo cur_setpoint(before update)=%d, "
			"update =%d\n", s->cur_setpoint, offset_change);
			
			s->cur_setpoint += offset_change;// (s->delta_ms - s->delta_ms_prev);
			
			//if ret is not zero, it means something is to be done for backup stuff
			ret = wrp->ops->adjust_phase(s->cur_setpoint, ppi->port_idx);

			// default, might change if resync requested from backup_state
			s->next_state = WR_TRACK_PHASE;
			s->state = WR_WAIT_SYNC_IDLE;

			if(ret) // check what is up 
			{
				uint32_t phase=0;
				int swover=0, resync=0;
				ss = (struct wrs_socket*)NP(ppi)->ch[PP_NP_GEN].arch_data;
				
				wrp->ops->backup_state(ppi->port_idx, &phase,&swover,&resync);

				pp_diag(ppi, servo, 1, "backup_state: good_phase_val=%d,"
					" cur_setpoint=%d, offset_change: %d, ts_offset=%d, "
					" dmtd_phase=%d, swover=%d, resync=%d\n",
					phase,s->cur_setpoint ,offset_change, ts_offset_hw.phase,
					ss->dmtd_phase, swover, resync);
				if(swover) // switchover occured and this is the new active channel
				{
					pp_diag(ppi, servo, 1, "@Switchover %d -> %d\n",
					cur_servo_state.active_port,  active_port);
					cur_servo_state.active_port = active_port;
					if(s->cur_setpoint < 0)
					{
						pp_diag(ppi, servo, 1, "we have a problem.. "
						"setpoint is negative: %d \n",s->cur_setpoint);
					}
					s->cur_setpoint = phase + ts_offset_hw.phase;
				}
				else if(resync) // this is backup and needs resynchronization
				{
					pp_diag(ppi, servo, 1, "@Resync\n");
					s->state = WR_SYNC_PHASE;
					
				}		
			}
			s->delta_ms_prev = s->delta_ms;			  
			s->last_tics = tics;
		}
		break;

	}
	return 0;
}
