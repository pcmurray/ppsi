/*
 * FIXME header
 */

#include <pproto/pproto.h>

/* FIXME: This is a temp workaround. How to define it? */
#define PP_INT_MAX 2147483647

void int64_to_TimeInternal(Integer64 bigint, TimeInternal *internal)
{
	int64_t bigint_val;

	bigint_val = bigint.lsb;
	bigint_val+= ((int64_t)bigint.msb) << 32;

	internal->nanoseconds = bigint_val % PP_NSEC_PER_SEC;
	internal->seconds = bigint_val / PP_NSEC_PER_SEC;
}

void from_TimeInternal(TimeInternal *internal, Timestamp *external)
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

	if ((internal->seconds & ~PP_INT_MAX) ||
	    (internal->nanoseconds & ~PP_INT_MAX)) {
		/* FIXME diag
		 * DBG("Negative value cannot be converted into timestamp \n");
		 */
		return;
	} else {
		external->secondsField.lsb = internal->seconds;
		external->nanosecondsField = internal->nanoseconds;
		external->secondsField.msb = 0;
	}
}

void to_TimeInternal(TimeInternal *internal, Timestamp *external)
{
	/* Program will not run after 2038... */
	if (external->secondsField.lsb < PP_INT_MAX) {
		internal->seconds = external->secondsField.lsb;
		internal->nanoseconds = external->nanosecondsField;
	} else {
		/* FIXME diag
		DBG("Clock servo canno't be executed : "
		    "seconds field is higher than signed integer (32bits) \n");
		*/
		return;
	}
}

void normalize_TimeInternal(TimeInternal *r)
{
	r->seconds+= r->nanoseconds / PP_NSEC_PER_SEC;
	r->nanoseconds-= r->nanoseconds / PP_NSEC_PER_SEC * PP_NSEC_PER_SEC;

	if (r->seconds > 0 && r->nanoseconds < 0) {
		r->seconds-= 1;
		r->nanoseconds+= PP_NSEC_PER_SEC;
	} else if (r->seconds < 0 && r->nanoseconds > 0) {
		r->seconds+= 1;
		r->nanoseconds-= PP_NSEC_PER_SEC;
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
