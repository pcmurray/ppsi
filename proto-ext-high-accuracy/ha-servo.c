#include <ppsi/ppsi.h>
#include "ha-api.h"
#include "wr-api.h"
#include <libwr/shmem.h>
#include <math.h>

#ifdef CONFIG_ARCH_WRS
#define ARCH_IS_WRS 1
#else
#define ARCH_IS_WRS 0
#endif

#define WR_SERVO_OFFSET_STABILITY_THRESHOLD 60 /* psec */

#define FIX_ALPHA_FRACBITS 40

/* Define threshold values for SNMP */
#define SNMP_MAX_OFFSET_PS 500
#define SNMP_MAX_DELTA_RTT_PS 1000

static const char *servo_name[] = {
	[WR_UNINITIALIZED] = "Uninitialized",
	[WR_SYNC_NSEC] = "SYNC_NSEC",
	[WR_SYNC_TAI] = "SYNC_SEC",
	[WR_SYNC_PHASE] = "SYNC_PHASE",
	[WR_TRACK_PHASE] = "TRACK_PHASE",
	[WR_WAIT_OFFSET_STABLE] = "WAIT_OFFSET_STABLE",
};

/* Enable tracking by default. Disabling the tracking is used for demos. */
static int tracking_enabled = 1;
extern struct wrs_shm_head *ppsi_head;

void wr_servo_enable_tracking(int enable)
{
	tracking_enabled = enable;
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

static int got_sync = 0;

void wr_servo_reset(struct pp_instance *ppi)
{
	/* values from servo_state to be preserved */
	uint32_t n_err_state;
	uint32_t n_err_offset;
	uint32_t n_err_delta_rtt;

	struct wr_servo_state *s;

	s = &((struct wr_data *)ppi->ext_data)->servo_state;
	if (!s) {
		/* Don't clean servo state when is not available */
		return;
	}
	/* shmem lock */
	wrs_shm_write(ppsi_head, WRS_SHM_WRITE_BEGIN);
	ppi->flags = 0;

	/* preserve some values from servo_state */
	n_err_state = s->n_err_state;
	n_err_offset = s->n_err_offset;
	n_err_delta_rtt = s->n_err_delta_rtt;
	/* clear servo_state to display empty fields in wr_mon and SNMP */
	memset(s, 0, sizeof(struct wr_servo_state));
	/* restore values from servo_state */
	s->n_err_state = n_err_state;
	s->n_err_offset = n_err_offset;
	s->n_err_delta_rtt = n_err_delta_rtt;

	/* shmem unlock */
	wrs_shm_write(ppsi_head, WRS_SHM_WRITE_END);
}

static inline int32_t delta_to_ps(struct FixedDelta d)
{
	UInteger64 *sps = &d.scaledPicoseconds; /* ieee type :( */

	return (sps->lsb >> 16) | (sps->msb << 16);
}

int wr_servo_init(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	struct wr_servo_state *s =
			&((struct wr_data *)ppi->ext_data)->servo_state;
	/* shmem lock */
	wrs_shm_write(ppsi_head, WRS_SHM_WRITE_BEGIN);
	/* Determine the alpha coefficient */
	if (wrp->ops->read_calib_data(ppi, 0, 0,
		&s->fiber_fix_alpha, &s->clock_period_ps) != WR_HW_CALIB_OK)
		return -1;

	wrp->ops->enable_timing_output(ppi, 0);

	strncpy(s->if_name, ppi->cfg.iface_name, sizeof(s->if_name));
	s->cur_setpoint = 0;
	wrp->ops->adjust_phase(s->cur_setpoint);
	s->missed_iters = 0;
	s->state = WR_SYNC_TAI;

	strcpy(s->servo_state_name, "Uninitialized");

	s->flags |= WR_FLAG_VALID;
	s->update_count = 0;
	s->tracking_enabled = tracking_enabled;

	got_sync = 0;

	/* shmem unlock */
	wrs_shm_write(ppsi_head, WRS_SHM_WRITE_END);
	return 0;
}

int wr_servo_got_sync(struct pp_instance *ppi, TimeInternal *t1,
		      TimeInternal *t2)
{
	struct wr_servo_state *s =
			&((struct wr_data *)ppi->ext_data)->servo_state;

	s->t1 = *t1;
	s->t1.correct = 1;
	s->t2 = *t2;

	got_sync = 1;

	return 0;
}

int wr_servo_got_delay(struct pp_instance *ppi, Integer32 cf)
{
	struct wr_servo_state *s =
			&((struct wr_data *)ppi->ext_data)->servo_state;

	wrs_shm_write(ppsi_head, WRS_SHM_WRITE_BEGIN);

	s->t3 = ppi->t3;
	/*  s->t3.phase = 0; */
	s->t4 = ppi->t4;
	s->t4.correct = 1; /* clock->delay_req_receive_time.correct; */
	s->t4.phase = (int64_t) cf * 1000LL / 65536LL;

	if (CONFIG_HAS_P2P && ppi->mech == PP_P2P_MECH) {
		s->t5 = ppi->t5;
		s->t5.correct = 1;
		s->t5.phase = 0;
		s->t6 = ppi->t6;
		s->t6.phase = (int64_t) ppi->t6_cf * 1000LL / 65536LL;

		wr_p2p_delay(ppi, s);
	}

	wrs_shm_write(ppsi_head, WRS_SHM_WRITE_END);
	return 0;
}

int wr_p2p_delay(struct pp_instance *ppi, struct wr_servo_state *s)
{
	uint64_t big_delta_fix;
	static int errcount;

	if (!s->t3.correct || !s->t4.correct ||
	    !s->t5.correct || !s->t6.correct) {
		errcount++;
		if (errcount > 5)	/* a 2-3 in a row are expected */
			pp_error("%s: TimestampsIncorrect: %d %d %d %d\n",
				 __func__, s->t3.correct, s->t4.correct,
				 s->t5.correct, s->t6.correct);
		return 0;
	}
	errcount = 0;

	s->update_count++;

	s->mu = ts_sub(ts_sub(s->t6, s->t3), ts_sub(s->t5, s->t4));

	if (__PP_DIAG_ALLOW(ppi, pp_dt_servo, 1)) {
		dump_timestamp(ppi, "servo:t1", s->t1);
		dump_timestamp(ppi, "servo:t2", s->t2);
		dump_timestamp(ppi, "servo:t3", s->t3);
		dump_timestamp(ppi, "servo:t4", s->t4);
		dump_timestamp(ppi, "servo:t5", s->t5);
		dump_timestamp(ppi, "servo:t6", s->t6);
		dump_timestamp(ppi, "->mdelay", s->mu);
	}

	s->picos_mu = ts_to_picos(s->mu);
	big_delta_fix = s->delta_tx_m + s->delta_tx_s
	    + s->delta_rx_m + s->delta_rx_s;

	s->delta_ms =
	    (((int64_t) (ts_to_picos(s->mu) - big_delta_fix) *
	      (int64_t) s->fiber_fix_alpha) >> FIX_ALPHA_FRACBITS)
	    + ((ts_to_picos(s->mu) - big_delta_fix) >> 1)
	    + s->delta_tx_m + s->delta_rx_s;

	return 1;
}

int wr_p2p_offset(struct pp_instance *ppi,
		  struct wr_servo_state *s, TimeInternal *ts_offset_hw)
{
	TimeInternal ts_offset;
	static int errcount;

	if (!s->t1.correct || !s->t2.correct) {
		errcount++;
		if (errcount > 5)	/* a 2-3 in a row are expected */
			pp_error("%s: TimestampsIncorrect: %d %d \n",
				 __func__, s->t1.correct, s->t2.correct);
		return 0;
	}
	errcount = 0;
	got_sync = 0;

	s->update_count++;

	ts_offset = ts_add(ts_sub(s->t1, s->t2), picos_to_ts(s->delta_ms));
	*ts_offset_hw = ts_hardwarize(ts_offset, s->clock_period_ps);

	/* is it possible to calculate it in client,
	 * but then t1 and t2 require shmem locks */
	s->offset = ts_to_picos(ts_offset);

	s->tracking_enabled =  tracking_enabled;

	return 1;
}

int wr_delay_ms_cal(struct pp_instance *ppi, struct wr_servo_state *s,
			TimeInternal *ts_offset_hw, int64_t*ts_offset_ps,
			int64_t *delay_ms_fix, int64_t *fiber_fix_alpha, 
			int64_t or_picos_mu, int64_t or_fiber_fix_alpha)
{
	int64_t big_delta_fix, fiber_fix_alpha_wr, delay_ms_fix_wr, ts_offset_ps_wr, picos_mu;
	TimeInternal ts_offset_wr, ts_offset_hw_wr;

	if(or_picos_mu)
		picos_mu = or_picos_mu;
	else
		picos_mu = s->picos_mu;
	
	if(or_fiber_fix_alpha)
		fiber_fix_alpha_wr = or_fiber_fix_alpha;
	else
		fiber_fix_alpha_wr = (int64_t)s->fiber_fix_alpha;
	
	big_delta_fix =  s->delta_tx_m + s->delta_tx_s
		       + s->delta_rx_m + s->delta_rx_s;

	
	delay_ms_fix_wr = (((int64_t)(picos_mu - big_delta_fix) * fiber_fix_alpha_wr) >> FIX_ALPHA_FRACBITS)
		+ ((picos_mu - big_delta_fix) >> 1)
		+ s->delta_tx_m + s->delta_rx_s;

	ts_offset_wr     = ts_add(ts_sub(s->t1, s->t2), picos_to_ts(delay_ms_fix_wr));
	ts_offset_hw_wr  = ts_hardwarize(ts_offset_wr, s->clock_period_ps);
	ts_offset_ps_wr  = ts_to_picos(ts_offset_wr);
	
	pp_diag(ppi, servo, 2, "ML: ===================== WR calculations ========================\n");
	pp_diag(ppi, servo, 2, "ML: inputs:\n");
	pp_diag(ppi, servo, 2, "ML:        s->fiber_fix_alpha          (int32_t) = %lld\n",(long long)fiber_fix_alpha_wr);
	pp_diag(ppi, servo, 2, "ML:        big_delta_fix               (int32_t) = %lld\n",(long long)big_delta_fix);
	pp_diag(ppi, servo, 2, "ML:        s->delta_tx_m+s->delta_rx_s (int32_t) = %lld\n",(long long)(s->delta_tx_m+s->delta_rx_s));
	pp_diag(ppi, servo, 2, "ML:        delayMM                     (int64_t) = %lld\n",(long long)(picos_mu));
	pp_diag(ppi, servo, 2, "ML: result:\n");
	pp_diag(ppi, servo, 2, "ML:        delayMS                     (int64_t) = %lld\n",(long long)(delay_ms_fix_wr));
	
	if(ts_offset_hw)
		*ts_offset_hw = ts_offset_hw_wr;
	if(ts_offset_ps)
		*ts_offset_ps=ts_offset_ps_wr;
	if(delay_ms_fix)
		*delay_ms_fix= delay_ms_fix_wr;
	if(fiber_fix_alpha)
		*fiber_fix_alpha=fiber_fix_alpha_wr;
	return 1;
}

int ha_delay_ms_cal(struct pp_instance *ppi, struct wr_servo_state *s,
			TimeInternal *ts_offset_hw, int64_t*ts_offset_ps,
			int64_t *delay_ms_fix, int64_t *fiber_fix_alpha, 
			int64_t or_picos_mu, int64_t or_delayCoeff)
{
	int64_t delayCoeff, fiber_fix_alpha_ha, delay_ms_fix_ha, ts_offset_ps_ha, picos_mu,fiber_fix_alpha_ha_double, port_fix_alpha, fiber_fix_alpha_ha_trick,sDC;
	TimeInternal ts_offset_ha, ts_offset_hw_ha;
	double alpha;
	
	
	if(or_picos_mu)
		picos_mu = or_picos_mu;
	else
		picos_mu = s->picos_mu;
	
	if(or_delayCoeff)
		delayCoeff = or_delayCoeff;
	else
		delayCoeff = ppi->asymCorrDS->delayCoefficient.scaledRelativeDifference;
	
		// for clarity and convenience
	#define RD_FR REL_DIFF_FRACBITS   /* = 62*/
	#define FA_FR FIX_ALPHA_FRACBITS  /* = 40*/
	
	/** computation of fiber_fixed_alpha directly from delayCoefficient in fix aritmethics:
	 * fix_alpha = [2^62 + delayCoeff]\[2*2^22 + delayCoeff *2-22] - 2^39 */
	fiber_fix_alpha_ha        = ((((int64_t)1<<62) + delayCoeff)/(((int64_t)1<<23) + (delayCoeff>>40)) - ((int64_t)1<<39));
	fiber_fix_alpha_ha_double = ((double)pow(2.0, 62.0) + (double)delayCoeff)/((double)pow(2.0, 23.0) + (double)(delayCoeff>>40)) - (double)pow(2.0, 39.0);
	
	sDC = (delayCoeff >> 32);
	
	if(delayCoeff > 0)
		fiber_fix_alpha_ha_trick  = +( delayCoeff  >>24)
		                            -((sDC*sDC)    >>23)
		                            +((sDC*sDC*sDC)>>54);
	else
		fiber_fix_alpha_ha_trick  = +( delayCoeff  >>24)
		                            +((sDC*sDC)    >>23)
		                            +((sDC*sDC*sDC)>>54);


	alpha                     = ((double)delayCoeff)/(double)pow(2.0, 62.0);
	port_fix_alpha            =  (double)pow(2.0, 40.0) * ((alpha + 1.0) / (alpha + 2.0) - 0.5);
	
	delay_ms_fix_ha = (((int64_t)picos_mu * fiber_fix_alpha_ha_trick) >> FIX_ALPHA_FRACBITS) + (picos_mu >> 1);

	ts_offset_ha = ts_add(ts_sub(s->t1, s->t2), picos_to_ts(delay_ms_fix_ha));
	ts_offset_hw_ha = ts_hardwarize(ts_offset_ha, s->clock_period_ps);
	ts_offset_ps_ha = ts_to_picos(ts_offset_ha);

	pp_diag(ppi, servo, 2, "ML: ===================== HA calculations ========================\n");
	pp_diag(ppi, servo, 2, "ML: inputs:\n");
	pp_diag(ppi, servo, 2, "ML:        delayCoeff                  (int64_t) = %lld\n",(long long)delayCoeff);
	pp_diag(ppi, servo, 2, "ML:        fiber_fix_alpha_ha          (int64_t) = %lld\n",(long long)fiber_fix_alpha_ha);
	pp_diag(ppi, servo, 2, "ML:        fiber_fix_alpha_ha_double   (int64_t) = %lld\n",(long long)fiber_fix_alpha_ha_double);
	pp_diag(ppi, servo, 2, "ML:        fiber_fix_alpha_ha_trick    (int64_t) = %lld\n",(long long)fiber_fix_alpha_ha_trick);
	pp_diag(ppi, servo, 1, "ML:        alpha                                 = %lld * e-10\n", (long long)(alpha*(int64_t)10000000000));
	pp_diag(ppi, servo, 2, "ML:        fiber_fix_alpha             (int64_t) = %lld\n",(long long)port_fix_alpha);
	pp_diag(ppi, servo, 2, "ML:        delayMM                     (int64_t) = %lld\n",(long long)(picos_mu));
	pp_diag(ppi, servo, 2, "ML: result:\n");
	pp_diag(ppi, servo, 2, "ML:        delayMS                     (int64_t) = %lld\n",(long long)(delay_ms_fix_ha));


	if(delay_ms_fix)
		*delay_ms_fix= delay_ms_fix_ha;
	if(ts_offset_hw)
		*ts_offset_hw = ts_offset_hw_ha;
	if(ts_offset_ps)
		*ts_offset_ps=ts_offset_ps_ha;
	if(fiber_fix_alpha)
		*fiber_fix_alpha=fiber_fix_alpha_ha;
	return 1;
}

int wr_e2e_offset(struct pp_instance *ppi,
		  struct wr_servo_state *s, TimeInternal *ts_offset_hw)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	
	int64_t fiber_fix_alpha_ha, fiber_fix_alpha_wr, port_fix_alpha_ha;
	int64_t delay_ms_fix_wr, delay_ms_fix_ha, delayCoeff_pos, delayCoeff_neg;
	int64_t ts_offset_ps_ha, ts_offset_ps_wr;
	double alpha_ha;
	TimeInternal ts_offset_hw_ha, ts_offset_hw_wr;

	static int errcount;

	if(!s->t1.correct || !s->t2.correct ||
	   !s->t3.correct || !s->t4.correct) {
		errcount++;
		if (errcount > 5) /* a 2-3 in a row are expected */
			pp_error("%s: TimestampsIncorrect: %d %d %d %d\n",
				 __func__, s->t1.correct, s->t2.correct,
				 s->t3.correct, s->t4.correct);
		return 0;
	}

	if (wrp->ops->servo_hook)
		wrp->ops->servo_hook(s, WR_SERVO_ENTER);

	errcount = 0;

	s->update_count++;
	ppi->t_ops->get(ppi, &s->update_time);

	got_sync = 0;

	s->mu = ts_sub(ts_sub(s->t4, s->t1), ts_sub(s->t3, s->t2));

	if (__PP_DIAG_ALLOW(ppi, pp_dt_servo, 1)) {
		dump_timestamp(ppi, "servo:t1", s->t1);
		dump_timestamp(ppi, "servo:t2", s->t2);
		dump_timestamp(ppi, "servo:t3", s->t3);
		dump_timestamp(ppi, "servo:t4", s->t4);
		dump_timestamp(ppi, "->mdelay", s->mu);
	}

	s->picos_mu = ts_to_picos(s->mu);

	/** do WR and HA calculations */
	wr_delay_ms_cal(ppi,s,&ts_offset_hw_wr, &ts_offset_ps_wr, &delay_ms_fix_wr, &fiber_fix_alpha_wr, 0,0);
	ha_delay_ms_cal(ppi,s,&ts_offset_hw_ha, &ts_offset_ps_ha, &delay_ms_fix_ha, &fiber_fix_alpha_ha, 0,0);

	/** my fake calculations for WR */
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000        /*1m*/      , 73621684 /*switch (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000     /*1 km*/    , 73621684 /*switch (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000    /*10km*/    , 73621684 /*switch (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000000  /*1 000km*/ , 73621684 /*switch (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000000 /*10 000km*/, 73621684 /*switch (+) alpha*/);

	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000        /*1m*/      ,-73621684 /*switch (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000     /*1 km*/    ,-73621684 /*switch (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000    /*10km*/    ,-73621684 /*switch (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000000  /*1 000km*/ ,-73621684 /*switch (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000000 /*10 000km*/,-73621684 /*switch (-) alpha*/);

	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000        /*1m*/      , 72169888 /*wrpc   (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000     /*1 km*/    , 72169888 /*wrpc   (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000    /*10km*/    , 72169888 /*wrpc   (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000000  /*1 000km*/ , 72169888 /*wrpc   (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000000 /*10 000km*/, 72169888 /*wrpc   (+) alpha*/);

	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000        /*1m*/      ,-73685416 /*wrpc   (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000     /*1 km*/    ,-73685416 /*wrpc   (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000    /*10km*/    ,-73685416 /*wrpc   (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000000  /*1 000km*/ ,-73685416 /*wrpc   (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000000 /*10 000km*/,-73685416 /*wrpc   (-) alpha*/);

	
	/** my fake calculations for HA */
	// negative
	delayCoeff_pos = 1235332333756144;
	delayCoeff_neg = (delayCoeff_pos/(((int64_t)1<<62) +delayCoeff_pos))<<62;
	
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000        /*1m*/      , 1235332333756144 /*switch (+) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000     /*1 km*/    , 1235332333756144 /*switch (+) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000    /*10km*/    , 1235332333756144 /*switch (+) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000000  /*1 000km*/ , 1235332333756144 /*switch (+) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000000 /*10 000km*/, 1235332333756144 /*switch (+) delayCoeff*/);

	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000        /*1m*/      ,-1235001513901056 /*switch (-) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000     /*1 km*/    ,-1235001513901056 /*switch (-) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000    /*10km*/    ,-1235001513901056 /*switch (-) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000000  /*1 000km*/ ,-1235001513901056 /*switch (-) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000000 /*10 000km*/,-1235001513901056 /*switch (-) delayCoeff*/);

	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000        /*1m*/      , delayCoeff_neg   /*switch (-) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000     /*1 km*/    , delayCoeff_neg   /*switch (-) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000    /*10km*/    , delayCoeff_neg   /*switch (-) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000000  /*1 000km*/ , delayCoeff_neg   /*switch (-) delayCoeff*/);
	ha_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000000 /*10 000km*/, delayCoeff_neg   /*switch (-) delayCoeff*/);
	
	/** my fake calculations for HA-dirty-impl */

       pp_diag(ppi, servo, 1, "ML\nML\n ML ============= UGLY HA imp (+)  ============= \nML\nML\nML");
	alpha_ha           =  ((double)delayCoeff_pos)/(double)pow(2.0, 62.0);
	port_fix_alpha_ha  =  (double)pow(2.0, 40.0) * ((alpha_ha + 1.0) / (alpha_ha + 2.0) - 0.5);
	pp_diag(ppi, servo, 1, "ML:        alpha = %lld * e-10 from delayCoeff = %lld\n",
		(long long)(alpha_ha*(int64_t)10000000000), (long long)delayCoeff_pos );
	
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000        /*1m*/      , port_fix_alpha_ha /*switch (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000     /*1 km*/    , port_fix_alpha_ha /*switch (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000    /*10km*/    , port_fix_alpha_ha /*switch (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000000  /*1 000km*/ , port_fix_alpha_ha /*switch (+) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000000 /*10 000km*/, port_fix_alpha_ha /*switch (+) alpha*/);

	pp_diag(ppi, servo, 1, "ML\nML\n ML ============= UGLY HA imp (+)  ============= \nML\nML\nML");
	delayCoeff_neg     = -1235001513901056;
	alpha_ha           = ((double)delayCoeff_neg)/(double)pow(2.0, 62.0);
	port_fix_alpha_ha  =  (double)pow(2.0, 40.0) * ((alpha_ha + 1.0) / (alpha_ha + 2.0) - 0.5);
	pp_diag(ppi, servo, 1, "ML:        alpha = %lld * e-10 from delayCoeff = %lld\n",
		(long long)(alpha_ha*(int64_t)10000000000), (long long)delayCoeff_neg );
	
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000        /*1m*/      ,port_fix_alpha_ha /*switch (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000     /*1 km*/    ,port_fix_alpha_ha /*switch (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000    /*10km*/    ,port_fix_alpha_ha /*switch (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 10000000000  /*1 000km*/ ,port_fix_alpha_ha /*switch (-) alpha*/);
	wr_delay_ms_cal(ppi,s, 0, 0, 0, 0, 100000000000 /*10 000km*/,port_fix_alpha_ha /*switch (-) alpha*/);

	
	/** compare WR and HA calculations */
	pp_diag(ppi, servo, 2, "ML: fiber_fix_alpha_wr (int64_t) = %lld\n",(long long)fiber_fix_alpha_wr);
	pp_diag(ppi, servo, 2, "ML: fiber_fix_alpha_wr (int32_t) = %d\n",  (int32_t)fiber_fix_alpha_wr);
	pp_diag(ppi, servo, 2, "ML: fiber_fix_alpha_ha (int64_t) = %lld\n",(long long)fiber_fix_alpha_ha);
	pp_diag(ppi, servo, 2, "ML: fiber_fix_alpha_ha (int32_t) = %d\n",  (int32_t)fiber_fix_alpha_ha);
	
	pp_diag(ppi, servo, 2, "ML: delay_ms_fix_wr    (int64_t) = %lld \n", (long long)delay_ms_fix_wr);
	pp_diag(ppi, servo, 2, "ML: delay_ms_fix_ha    (int64_t) = %lld \n", (long long)delay_ms_fix_ha);

	pp_diag(ppi, servo, 2, "ML: ts_offset_wr       (int64_t) = %lld [ps] \n", (long long)ts_offset_ps_wr);
	pp_diag(ppi, servo, 2, "ML: ts_offset_ha       (int64_t) = %lld [ps] \n", (long long)ts_offset_ps_ha);
	
	dump_timestamp(ppi,    "ML: ts_offset_hw_wr    (int64_t)",ts_offset_hw_wr);
	dump_timestamp(ppi,    "ML: ts_offset_hw_ha    (int64_t)",ts_offset_hw_ha);

	/** use either WR or HA */
// 	*ts_offset_hw = ts_offset_hw_wr;
// 	s->offset     = ts_offset_ps_wr;
// 	s->delta_ms   = delay_ms_fix_wr;
	
	*ts_offset_hw = ts_offset_hw_ha;
	s->offset     = ts_offset_ps_ha;
	s->delta_ms   = delay_ms_fix_ha;
	
	s->tracking_enabled =  tracking_enabled;
	
	return 1;
}

int wr_servo_update(struct pp_instance *ppi)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	struct wr_servo_state *s =
	    &((struct wr_data *)ppi->ext_data)->servo_state;
	int remaining_offset;
	int64_t picos_mu_prev = 0;

	TimeInternal ts_offset_hw /*, ts_phase_adjust */ ;

	if (!got_sync)
		return 0;

	/* shmem lock */
	wrs_shm_write(ppsi_head, WRS_SHM_WRITE_BEGIN);

	picos_mu_prev = s->picos_mu;
	if (CONFIG_HAS_P2P && ppi->mech == PP_P2P_MECH) {
		if (!wr_p2p_offset(ppi, s, &ts_offset_hw))
			goto out;
	} else {
		if (!wr_e2e_offset(ppi, s, &ts_offset_hw))
			goto out;
	}

	if (wrp->ops->locking_poll(ppi, 0) != WR_SPLL_READY) {
		pp_diag(ppi, servo, 1, "PLL OutOfLock, should restart sync\n");
		wrp->ops->enable_timing_output(ppi, 0);
		/* TODO check
		 * DSPOR(ppi)->doRestart = TRUE; */
	}

	/* After each action on the hardware, we must verify if it is over. */
	if (!wrp->ops->adjust_in_progress()) {
		s->flags &= ~WR_FLAG_WAIT_HW;
	} else {
		pp_diag(ppi, servo, 1, "servo:busy\n");
		goto out;
	}

	/* So, we didn't return. Choose the right state */
	if (ts_offset_hw.seconds) /* so bad... */
		s->state = WR_SYNC_TAI;
	else if (ts_offset_hw.nanoseconds) /* not that bad */
		s->state = WR_SYNC_NSEC;
	/* else, let the states below choose the sequence */

	pp_diag(ppi, servo, 2, "offset_hw: %li.%09li (+%li)\n",
		(long)ts_offset_hw.seconds, (long)ts_offset_hw.nanoseconds,
		(long)ts_offset_hw.phase);

	pp_diag(ppi, servo, 1, "wr_servo state: %s%s\n",
		servo_name[s->state],
		s->flags & WR_FLAG_WAIT_HW ? " (wait for hw)" : "");

	/* update string state name */
	strcpy(s->servo_state_name, servo_name[s->state]);

	switch (s->state) {
	case WR_SYNC_TAI:
		wrp->ops->adjust_counters(ts_offset_hw.seconds, 0);
		s->flags |= WR_FLAG_WAIT_HW;
		/*
		 * If nsec wrong, code above forces SYNC_NSEC,
		 * Else, we must ensure we leave this status towards
		 * fine tuning
		 */
		s->state = WR_SYNC_PHASE;
		break;

	case WR_SYNC_NSEC:
		wrp->ops->adjust_counters(0, ts_offset_hw.nanoseconds);
		s->flags |= WR_FLAG_WAIT_HW;
		s->state = WR_SYNC_PHASE;
		break;

	case WR_SYNC_PHASE:
		pp_diag(ppi, servo, 2, "oldsetp %i, offset %i:%04i\n",
			s->cur_setpoint, ts_offset_hw.nanoseconds,
			ts_offset_hw.phase);
		s->cur_setpoint += ts_offset_hw.phase;
		wrp->ops->adjust_phase(s->cur_setpoint);

		s->flags |= WR_FLAG_WAIT_HW;
		s->state = WR_WAIT_OFFSET_STABLE;

		if (ARCH_IS_WRS) {
			/*
			 * Now, let's fix system time. We pass here
			 * once only, so that's the best place to do
			 * it. We can't use current WR time, as we
			 * still miss the method to get it (through IPC).
			 * So use T4, which is a good approximation.
			 */
			unix_time_ops.set(ppi, &ppi->t4);
			pp_diag(ppi, time, 1, "system time set to %li TAI\n",
				(long)ppi->t4.seconds);
		}
		break;

	case WR_WAIT_OFFSET_STABLE:

		/* ts_to_picos() below returns phase alone */
		remaining_offset = abs(ts_to_picos(ts_offset_hw));
		if(remaining_offset < WR_SERVO_OFFSET_STABILITY_THRESHOLD) {
			wrp->ops->enable_timing_output(ppi, 1);
			s->delta_ms_prev = s->delta_ms;
			s->state = WR_TRACK_PHASE;
		} else {
			s->missed_iters++;
		}
		if (s->missed_iters >= 10) {
			s->missed_iters = 0;
			s->state = WR_SYNC_PHASE;
		}
		break;

	case WR_TRACK_PHASE:
		s->skew = s->delta_ms - s->delta_ms_prev;

		/* Can be disabled for manually tweaking and testing */
		if(tracking_enabled) {
			if (abs(ts_offset_hw.phase) >
			    2 * WR_SERVO_OFFSET_STABILITY_THRESHOLD) {
				s->state = WR_SYNC_PHASE;
				break;
			}

			// adjust phase towards offset = 0 make ck0 0
			s->cur_setpoint += (ts_offset_hw.phase / 4);

			wrp->ops->adjust_phase(s->cur_setpoint);
			pp_diag(ppi, time, 1, "adjust phase %i\n",
				s->cur_setpoint);

			s->delta_ms_prev = s->delta_ms;
		}
		break;

	}
	/* Increase number of servo updates with state different than
	 * WR_TRACK_PHASE. (Used by SNMP) */
	if (s->state != WR_TRACK_PHASE)
		s->n_err_state++;

	/* Increase number of servo updates with offset exceeded
	 * SNMP_MAX_OFFSET_PS (Used by SNMP) */
	if (abs(s->offset) > SNMP_MAX_OFFSET_PS)
		s->n_err_offset++;

	/* Increase number of servo updates with delta rtt exceeded
	 * SNMP_MAX_DELTA_RTT_PS (Used by SNMP) */
	if (abs(picos_mu_prev - s->picos_mu) > SNMP_MAX_DELTA_RTT_PS)
		s->n_err_delta_rtt++;

out:
	/* shmem unlock */
	wrs_shm_write(ppsi_head, WRS_SHM_WRITE_END);

	if (wrp->ops->servo_hook)
		wrp->ops->servo_hook(s, WR_SERVO_LEAVE);

	return 0;
}
