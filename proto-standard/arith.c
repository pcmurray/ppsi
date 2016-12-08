/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <limits.h>
#include <ppsi/ppsi.h>

void cField_to_TimeInternal(TimeInternal *internal, Integer64 cField)
{
	uint64_t i64;

	i64 = cField.lsb;
	i64 |= ((int64_t)cField.msb) << 32;

	if ((int32_t)cField.msb < 0)
		pp_error("BUG: %s doesn't support negatives\n", __func__);

	/*
	 * the correctionField is nanoseconds scaled by 16 bits.
	 * It is updated by transparent clocks and may be used to count
	 * for asymmetry. Since we support no better than nanosecond with
	 * the standard protocol and WR (which is better than nanosecond)
	 * doesn't use this field, just approximate to nanoseconds.
	 * and the WR extension uses its own methods for asymmetry,
	 */
	i64 += 0x8000;
	i64 >>= 16;
	/* Use __div64_32 from library, to avoid libgcc on small targets */
	internal->nanoseconds = __div64_32(&i64, PP_NSEC_PER_SEC);
	internal->seconds = i64;
}

int from_TimeInternal(TimeInternal *internal, Timestamp *external)
{
	/*
	 * fromInternalTime is only used to convert time given by the system
	 * to a timestamp As a consequence, no negative value can normally
	 * be found in (internal)
	 *
	 * Note that offsets are also represented with TimeInternal structure,
	 * and can be negative, but offset are never convert into Timestamp
	 * so there is no problem here.
	 */

	if ((internal->seconds & ~INT_MAX) ||
	    (internal->nanoseconds & ~INT_MAX)) {
		pp_error("Negative value (%08x.%08x) cannot be converted "
			 "into timestamp\n",
			 internal->seconds,
			 internal->nanoseconds);
		return -1;
	} else {
		external->secondsField.lsb = internal->seconds;
		external->nanosecondsField = internal->nanoseconds;
		external->secondsField.msb = 0;
	}
	return 0;
}

int to_TimeInternal(TimeInternal *internal, Timestamp *external)
{
	/* Program will not run after 2038... */
	if (external->secondsField.lsb < INT_MAX) {
		internal->seconds = external->secondsField.lsb;
		internal->nanoseconds = external->nanosecondsField;
		return 0;
	} else {
		pp_error("to_TimeInternal: "
		    "seconds field is higher than signed integer (32bits)\n");
		return -1;
	}
}

/* A negative TimeInternal has both secs and nsecs <= 0 */
static void normalize_TimeInternal(TimeInternal *r)
{
	r->seconds += r->nanoseconds / PP_NSEC_PER_SEC;
	r->nanoseconds -= r->nanoseconds / PP_NSEC_PER_SEC * PP_NSEC_PER_SEC;

	if (r->seconds > 0 && r->nanoseconds < 0) {
		r->seconds -= 1;
		r->nanoseconds += PP_NSEC_PER_SEC;
	} else if (r->seconds < 0 && r->nanoseconds > 0) {
		r->seconds += 1;
		r->nanoseconds -= PP_NSEC_PER_SEC;
	}
}

void add_TimeInternal(TimeInternal *r, TimeInternal *x, TimeInternal *y)
{
	r->seconds = x->seconds + y->seconds;
	r->nanoseconds = x->nanoseconds + y->nanoseconds;

	normalize_TimeInternal(r);
}

void sub_TimeInternal(TimeInternal *r, TimeInternal *x, TimeInternal *y)
{
	r->seconds = x->seconds - y->seconds;
	r->nanoseconds = x->nanoseconds - y->nanoseconds;

	normalize_TimeInternal(r);
}

void div2_TimeInternal(TimeInternal *r)
{
	r->nanoseconds += r->seconds % 2 * PP_NSEC_PER_SEC;
	r->seconds /= 2;
	r->nanoseconds /= 2;

	normalize_TimeInternal(r);
}
/** this stuff can be used in any impl why to keep it only to WR??*/
int64_t ts_to_picos(TimeInternal ts)
{
	return ts.seconds * 1000000000000LL
		+ ts.nanoseconds * 1000LL
		+ ts.phase;
}

TimeInternal picos_to_ts(int64_t picos)
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

TimeInternal ts_add(TimeInternal a, TimeInternal b)
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

TimeInternal ts_sub(TimeInternal a, TimeInternal b)
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

void dump_timestamp(struct pp_instance *ppi, char *what, TimeInternal ts)
{
	pp_diag(ppi, servo, 2, "%s = %d:%d:%d\n", what, (int32_t)ts.seconds,
		  ts.nanoseconds, ts.phase);
}