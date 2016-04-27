#ifndef __IEEE1588_TYPES_HELPERS_H__
#define __IEEE1588_TYPES_HELPERS_H__

#include <ppsi/ieee1588_types.h>
#include <arch/arch.h>

static inline void uint48_internal_to_wire(uint48_wire *dst,
					   const uint64_t *src)
{
	dst->lsdw = htonl(*src & 0xffffffffULL);
	dst->msw = htons(*src >> 32);
}

static inline void uint48_wire_to_internal(uint64_t *dst,
					   const uint48_wire *src)
{
	*dst = ntohl(src->lsdw) + (((uint64_t)ntohs(src->msw)) << 32);
}

static inline void uint64_internal_to_wire(uint64_wire *dst,
					   const uint64_t *src)
{
	dst->lsdw = htonl(*src & 0xffffffffULL);
	dst->msdw = htonl(*src >> 32);
}

static inline void uint64_wire_to_internal(uint64_t *dst,
					   const uint64_wire *src)
{
	*dst = ntohl(src->lsdw) + (((uint64_t)ntohl(src->msdw)) << 32);
}

static inline void int64_internal_to_wire(int64_wire *dst, const int64_t *src)
{
	dst->lsdw = htonl(*src & 0xffffffffULL);
	dst->msdw = htonl(*src >> 32);
}

static inline void int64_wire_to_internal(int64_t *dst, const int64_wire *src)
{
	*dst = ntohl(src->lsdw) + (((uint64_t)ntohl(src->msdw)) << 32);
}

static inline void timestamp_internal_to_wire(timestamp_wire *dst,
					      const Timestamp *src)
{
	uint48_internal_to_wire(&dst->secondsField, &src->secondsField);
	dst->nanosecondsField = htonl(src->nanosecondsField);
}

static inline void timestamp_wire_to_internal(Timestamp *dst,
					      const timestamp_wire *src)
{
	uint48_wire_to_internal(&dst->secondsField, &src->secondsField);
	dst->nanosecondsField = ntohl(src->nanosecondsField);
}

static inline void time_internal_to_timestamp_internal(Timestamp *dst,
						       const TimeInternal *src)
{
	dst->secondsField = src->seconds;
	dst->nanosecondsField = src->nanoseconds;
}

static inline void timestamp_internal_to_time_internal(TimeInternal *dst,
						       const Timestamp *src)
{
	dst->seconds = src->secondsField;
	dst->nanoseconds = src->nanosecondsField;
}

static inline int clock_id_cmp(const struct clock_identity *a,
			       const struct clock_identity *b)
{
	return memcmp(a, b, sizeof(*a));
}

static inline int port_id_cmp(const struct port_identity *a,
			      const struct port_identity *b)
{
	int ret;

	ret = clock_id_cmp(&a->clockIdentity, &b->clockIdentity);
	if (ret)
		return ret;
	return a->portNumber - b->portNumber;
}
#endif /* __IEEE1588_TYPES_HELPERS_H__ */
