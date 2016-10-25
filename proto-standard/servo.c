/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <ppsi/ppsi.h>

static void pp_servo_mpd_fltr(struct pp_instance *, struct pp_avg_fltr *,
			      TimeInternal *);
static int pp_servo_offset_master(struct pp_instance *, TimeInternal *,
				   TimeInternal *, TimeInternal *);
static Integer32 pp_servo_pi_controller(struct pp_instance *, TimeInternal *);


void pp_servo_init(struct pp_instance *ppi)
{
	int d;

	SRV(ppi)->mpd_fltr.s_exp = 0;	/* clears meanPathDelay filter */
	ppi->frgn_rec_num = 0;		/* no known master */
	DSPAR(ppi)->parentPortIdentity.portNumber = 0; /* invalid */

	if (ppi->t_ops->init_servo) {
		/* The system may pre-set us to keep current frequency */
		d = ppi->t_ops->init_servo(ppi);
		if (d == -1) {
			pp_diag(ppi, servo, 1, "error in t_ops->servo_init");
			d = 0;
		}
		SRV(ppi)->obs_drift = -d << 10; /* note "-" */
	} else {
		/* level clock */
		if (pp_can_adjust(ppi))
			ppi->t_ops->adjust(ppi, 0, 0);
		SRV(ppi)->obs_drift = 0;
	}

	pp_diag(ppi, servo, 1, "Initialized: obs_drift %lli\n",
		SRV(ppi)->obs_drift);
}

/* internal helper, returning static storage to be used immediately */
static char *fmt_TI(TimeInternal *t)
{
	static char s[24];

	pp_sprintf(s, "%s%d.%09d",
		(t->seconds < 0 || (t->seconds == 0 && t->nanoseconds < 0))
		   ? "-" : " ",
		   (int)abs(t->seconds), (int)abs(t->nanoseconds));
	return s;
}

/* Called by slave and uncalib when we have t1 and t2 */
void pp_servo_got_sync(struct pp_instance *ppi)
{
	TimeInternal *m_to_s_dly = &SRV(ppi)->m_to_s_dly;

	/*
	 * calc 'master_to_slave_delay', removing the correction field
	 * added by transparent clocks in the path.
	 * cField contains the sum of sync and followup messages
	 * correctionField(s)
	 */
	sub_TimeInternal(m_to_s_dly, &ppi->t2, &ppi->t1);
	sub_TimeInternal(m_to_s_dly, m_to_s_dly, &ppi->cField);
	pp_diag(ppi, servo, 3, "correction field 1: %s\n",
		fmt_TI(&ppi->cField));
}

/* Called by slave and uncalib when we have t1 and t2 */
void pp_servo_got_psync(struct pp_instance *ppi)
{
	TimeInternal *m_to_s_dly = &SRV(ppi)->m_to_s_dly;
	TimeInternal *mpd = &DSCUR(ppi)->meanPathDelay;
	TimeInternal *ofm = &DSCUR(ppi)->offsetFromMaster;
	Integer32 adj;

	pp_diag(ppi, servo, 2, "T1: %s\n", fmt_TI(&ppi->t1));
	pp_diag(ppi, servo, 2, "T2: %s\n", fmt_TI(&ppi->t2));

	/*
	 * calc 'master_to_slave_delay', removing the correction field
	 * added by transparent clocks in the path.
	 */
	sub_TimeInternal(m_to_s_dly, &ppi->t2, &ppi->t1);
	sub_TimeInternal(m_to_s_dly, m_to_s_dly, &ppi->cField);
	pp_diag(ppi, servo, 3, "correction field 1: %s\n",
		fmt_TI(&ppi->cField));

	/* update 'offsetFromMaster' and possibly jump in time */
	if (pp_servo_offset_master(ppi, mpd, ofm, m_to_s_dly))
		return;

	/* PI controller */
	adj = pp_servo_pi_controller(ppi, ofm);

	/* apply controller output as a clock tick rate adjustment, if
	 * provided by arch, or as a raw offset otherwise */
	if (pp_can_adjust(ppi)) {
		if (ppi->t_ops->adjust_freq)
			ppi->t_ops->adjust_freq(ppi, -adj);
		else
			ppi->t_ops->adjust_offset(ppi, -adj);
	}

	pp_diag(ppi, servo, 2, "Observed drift: %9i\n",
		(int)SRV(ppi)->obs_drift >> 10);
}

/* called by slave states when delay_resp is received (all t1..t4 are valid) */
void pp_servo_got_resp(struct pp_instance *ppi)
{
	TimeInternal *m_to_s_dly = &SRV(ppi)->m_to_s_dly;
	TimeInternal *s_to_m_dly = &SRV(ppi)->s_to_m_dly;
	TimeInternal *mpd = &DSCUR(ppi)->meanPathDelay;
	TimeInternal *ofm = &DSCUR(ppi)->offsetFromMaster;
	struct pp_avg_fltr *mpd_fltr = &SRV(ppi)->mpd_fltr;
	Integer32 adj;


	/* We sometimes enter here before we got sync/f-up */
	if (ppi->t1.seconds == 0 && ppi->t1.nanoseconds == 0) {
		pp_diag(ppi, servo, 2, "discard T3/T4: we miss T1/T2\n");
		return;
	}
	/*
	 * calc 'slave_to_master_delay', removing delay_resp correction field
	 * added by transparent clocks in the path.
	 */
	sub_TimeInternal(s_to_m_dly, &ppi->t4,	&ppi->t3);
	sub_TimeInternal(s_to_m_dly, s_to_m_dly, &ppi->cField);
	pp_diag(ppi, servo, 3, "correction field 2: %s\n",
		fmt_TI(&ppi->cField));

	pp_diag(ppi, servo, 2, "T1: %s\n", fmt_TI(&ppi->t1));
	pp_diag(ppi, servo, 2, "T2: %s\n", fmt_TI(&ppi->t2));
	pp_diag(ppi, servo, 2, "T3: %s\n", fmt_TI(&ppi->t3));
	pp_diag(ppi, servo, 2, "T4: %s\n", fmt_TI(&ppi->t4));
	pp_diag(ppi, servo, 1, "Master to slave: %s\n", fmt_TI(m_to_s_dly));
	pp_diag(ppi, servo, 1, "Slave to master: %s\n", fmt_TI(s_to_m_dly));

	/* Calc mean path delay, used later to calc "offset from master" */
	add_TimeInternal(mpd, &SRV(ppi)->m_to_s_dly, &SRV(ppi)->s_to_m_dly);
	div2_TimeInternal(mpd);
	pp_diag(ppi, servo, 1, "meanPathDelay: %s\n", fmt_TI(mpd));

	if (mpd->seconds) /* Hmm.... we called this "bad event" */
		return;

	/* mean path delay filtering */
	pp_servo_mpd_fltr(ppi, mpd_fltr, mpd);

	/* update 'offsetFromMaster' and possibly jump in time */
	if (pp_servo_offset_master(ppi, mpd, ofm, m_to_s_dly))
		return;

	/* PI controller */
	adj = pp_servo_pi_controller(ppi, ofm);

	/* apply controller output as a clock tick rate adjustment, if
	 * provided by arch, or as a raw offset otherwise */
	if (pp_can_adjust(ppi)) {
		if (ppi->t_ops->adjust_freq)
			ppi->t_ops->adjust_freq(ppi, -adj);
		else
			ppi->t_ops->adjust_offset(ppi, -adj);
	}

	pp_diag(ppi, servo, 2, "Observed drift: %9i\n",
		(int)SRV(ppi)->obs_drift >> 10);
}

/* called by slave states when delay_resp is received (all t1..t4 are valid) */
void pp_servo_got_presp(struct pp_instance *ppi)
{
	TimeInternal *m_to_s_dly = &SRV(ppi)->m_to_s_dly;
	TimeInternal *s_to_m_dly = &SRV(ppi)->s_to_m_dly;
	TimeInternal *mpd = &DSCUR(ppi)->meanPathDelay;
	struct pp_avg_fltr *mpd_fltr = &SRV(ppi)->mpd_fltr;

	/*
	 * calc 'slave_to_master_delay', removing the correction field
	 * added by transparent clocks in the path.
	 */
	sub_TimeInternal(s_to_m_dly, &ppi->t6, &ppi->t5);
	sub_TimeInternal(s_to_m_dly, s_to_m_dly, &ppi->cField);
	pp_diag(ppi, servo, 3, "correction field 2: %s\n",
		fmt_TI(&ppi->cField));

	sub_TimeInternal(m_to_s_dly, &ppi->t4, &ppi->t3);

	pp_diag(ppi, servo, 2, "T3: %s\n", fmt_TI(&ppi->t3));
	pp_diag(ppi, servo, 2, "T4: %s\n", fmt_TI(&ppi->t4));
	pp_diag(ppi, servo, 2, "T5: %s\n", fmt_TI(&ppi->t5));
	pp_diag(ppi, servo, 2, "T6: %s\n", fmt_TI(&ppi->t6));
	pp_diag(ppi, servo, 1, "Master to slave: %s\n", fmt_TI(m_to_s_dly));
	pp_diag(ppi, servo, 1, "Slave to master: %s\n", fmt_TI(s_to_m_dly));

	/* Calc mean path delay, used later to calc "offset from master" */
	add_TimeInternal(mpd, &SRV(ppi)->m_to_s_dly, &SRV(ppi)->s_to_m_dly);
	div2_TimeInternal(mpd);
	pp_diag(ppi, servo, 1, "meanPathDelay: %s\n", fmt_TI(mpd));

	if (mpd->seconds) /* Hmm.... we called this "bad event" */
		return;

	pp_servo_mpd_fltr(ppi, mpd_fltr, mpd);
}

static
void pp_servo_mpd_fltr(struct pp_instance *ppi, struct pp_avg_fltr *mpd_fltr,
		       TimeInternal * mpd)
{
	int s;

	if (mpd_fltr->s_exp < 1) {
		/* First time, keep what we have */
		mpd_fltr->y = mpd->nanoseconds;
	}
	/* avoid overflowing filter */
	s = OPTS(ppi)->s;
	while (abs(mpd_fltr->y) >> (31 - s))
		--s;
	if (mpd_fltr->s_exp > 1 << s)
		mpd_fltr->s_exp = 1 << s;
	/* crank down filter cutoff by increasing 's_exp' */
	if (mpd_fltr->s_exp < 1 << s)
		++mpd_fltr->s_exp;

	/*
	 * It may happen that mpd appears as negative. This happens when
	 * the slave clock is running fast to recover a late time: the
	 * (t3 - t2) measured in the slave appears longer than the (t4 - t1)
	 * measured in the master.  Ignore such values, by keeping the
	 * current average instead.
	 */
	if (mpd->nanoseconds < 0)
		mpd->nanoseconds = mpd_fltr->y;
	if (mpd->nanoseconds < 0)
		mpd->nanoseconds = 0;

	/*
	 * It may happen that mpd appears to be very big. This happens
	 * when we have software timestamps and there is overhead
	 * involved -- or when the slave clock is running slow.  In
	 * this case use a value just slightly bigger than the current
	 * average (so if it really got longer, we will adapt).  This
	 * kills most outliers on loaded networks.
	 * The constant multipliers have been chosed arbitrarily, but
	 * they work well in testing environment.
	 */
	if (mpd->nanoseconds > 3 * mpd_fltr->y) {
		pp_diag(ppi, servo, 1, "Trim too-long mpd: %i\n",
			mpd->nanoseconds);
		/* add fltr->s_exp to ensure we are not trapped into 0 */
		mpd->nanoseconds = mpd_fltr->y * 2 + mpd_fltr->s_exp + 1;
	}
	/* filter 'meanPathDelay' (running average) */
	mpd_fltr->y = (mpd_fltr->y * (mpd_fltr->s_exp - 1) + mpd->nanoseconds)
	    / mpd_fltr->s_exp;
	mpd->nanoseconds = mpd_fltr->y;

	pp_diag(ppi, servo, 1, "After avg(%i), meanPathDelay: %i\n",
		(int)mpd_fltr->s_exp, mpd->nanoseconds);
}

static
int pp_servo_offset_master(struct pp_instance *ppi, TimeInternal * mpd,
			    TimeInternal * ofm, TimeInternal * m_to_s_dly)
{
	Integer32 adj;

	sub_TimeInternal(ofm, m_to_s_dly, mpd);
	pp_diag(ppi, servo, 1, "Offset from master:     %s\n", fmt_TI(ofm));

	if (ofm->seconds) {
		TimeInternal time_tmp;

		SRV(ppi)->obs_drift = 0; /* too long a jump; reset PI value */

		/* if secs, reset clock or set freq adjustment to max */
		if (pp_can_adjust(ppi)) {
			/* Can't use adjust, limited to +/- 2s */
			time_tmp = ppi->t4;
			add_TimeInternal(&time_tmp, &time_tmp,
					 &DSCUR(ppi)->meanPathDelay);
			ppi->t_ops->set(ppi, &time_tmp);
			pp_servo_init(ppi);
		}
		return 1; /* done */
	}
	return 0; /* not done: proceed with filtering */
}

static
Integer32 pp_servo_pi_controller(struct pp_instance * ppi, TimeInternal * ofm)
{
	long long I_term;
	long long P_term;
	long long tmp;
	int I_sign;
	int P_sign;
	Integer32 adj;

	/* the accumulator for the I component, shift by 10 to avoid losing bits
	 * later in the division */
	SRV(ppi)->obs_drift += ((long long)ofm->nanoseconds) << 10;

	/* Anti-windup. The PP_ADJ_FREQ_MAX value is multiplied by OPTS(ppi)->ai
	 * (which is the reciprocal of the integral gain of the controller).
	 * Then it's scaled by 10 bits to match the bit shift used earlier to
	 * avoid bit losses */
	tmp = (((long long)PP_ADJ_FREQ_MAX) * OPTS(ppi)->ai) << 10;
	if (SRV(ppi)->obs_drift > tmp)
		SRV(ppi)->obs_drift = tmp;
	else if (SRV(ppi)->obs_drift < -tmp)
		SRV(ppi)->obs_drift = -tmp;

	/* calculation of the I component, based on obs_drift */
	I_sign = (SRV(ppi)->obs_drift > 0) ? 0 : -1;
	I_term = SRV(ppi)->obs_drift;
	if (I_sign)
		I_term = -I_term;
	__div64_32((uint64_t *)&I_term, OPTS(ppi)->ai);
	if (I_sign)
		I_term = -I_term;

	/* calculation of the P component */
	P_sign = (ofm->nanoseconds > 0) ? 0 : -1;
	/* shift 10 bits again to avoid losses */
	P_term = ((long long)ofm->nanoseconds) << 10;
	if (P_sign)
		P_term = -P_term;
	__div64_32((uint64_t *)&P_term, OPTS(ppi)->ap);
	if (P_sign)
		P_term = -P_term;

	/* calculate the correction of applied by the controller */
	tmp = P_term + I_term;
	/* Now scale it before passing the argument to adjtimex
	 * Be careful with the signs */
	if (tmp > 0)
		adj = (tmp >> 10);
	else
		adj = -((-tmp) >> 10);

	return adj;
}
