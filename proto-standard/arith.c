/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on PTPd project v. 2.1.0 (see AUTHORS for details)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#include <limits.h>
#include <ppsi/ppsi.h>

void cField_to_TimeInternal(TimeInternal *dst, int64_t cField)
{
	uint64_t i64;

	if (cField < 0)
		pp_error("BUG: %s doesn't support negatives\n", __func__);
	i64 = cField;

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
	dst->nanoseconds = __div64_32(&i64, PP_NSEC_PER_SEC);
	dst->seconds = i64;
}

int from_TimeInternal(TimeInternal *src, Timestamp *dst)
{
	/*
	 * fromInternalTime is only used to convert time given by the system
	 * to a timestamp As a consequence, no negative value can normally
	 * be found in (src)
	 *
	 * Note that offsets are also represented with TimeInternal structure,
	 * and can be negative, but offset are never convert into Timestamp
	 * so there is no problem here.
	 */

	if ((src->seconds & ~INT_MAX) ||
	    (src->nanoseconds & ~INT_MAX)) {
		pp_error("Negative value cannot be converted into "
			  "timestamp\n");
		return -1;
	} else
		time_internal_to_timestamp_internal(dst, src);
	return 0;
}

int to_TimeInternal(TimeInternal *dst, Timestamp *src)
{
	/* Program will not run after 2038... */
	if (src->secondsField < INT_MAX) {
		timestamp_internal_to_time_internal(dst, src);
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
