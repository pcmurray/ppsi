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

static inline void msg_hdr_set_cf(struct msg_header_wire *hdr, uint64_t _cf)
{
	uint64_t cf = _cf;

	uint64_internal_to_wire(&hdr->cf, &cf);
}

static inline uint64_t msg_hdr_get_cf(const struct msg_header_wire *hdr)
{
	uint64_t v;

	uint64_wire_to_internal(&v, &hdr->cf);
	return v;
}

static inline uint8_t msg_hdr_get_msg_type(const struct msg_header_wire *hdr)
{
	return hdr->ts_mt & 0xf;
}

static inline uint16_t msg_hdr_get_msg_seq_id(const struct msg_header_wire *hdr)
{
	return ntohs(hdr->seq_id);
}

static inline uint8_t msg_hdr_get_msg_dn(const struct msg_header_wire *hdr)
{
	return hdr->domain;
}

static inline uint8_t
msg_hdr_get_log_msg_intvl(const struct msg_header_wire *hdr)
{
	return hdr->log_msg_intvl;
}

static inline void msg_hdr_copy(struct msg_header_wire *dst,
				const struct msg_header_wire *src)
{
	memcpy(dst, src, sizeof(*dst));
}

static inline void msg_hdr_get_src_port_id(struct port_identity *dst,
					   const struct msg_header_wire *hdr)
{
	memcpy(&dst->clockIdentity, hdr->spid.cid, sizeof(dst->clockIdentity));
	dst->portNumber = ntohs(hdr->spid.pn);
}

static inline void msg_hdr_set_src_port_id(struct msg_header_wire *hdr,
					   const struct port_identity *src)
{
	memcpy(hdr->spid.cid, &src->clockIdentity, sizeof(hdr->spid.cid));
	hdr->spid.pn = htons(src->portNumber);
}

static inline void
msg_hdr_set_src_port_id_clock_id(struct msg_header_wire *hdr,
				 const struct clock_identity *id)
{
	memcpy(hdr->spid.cid, id, sizeof(hdr->spid.cid));
}

static inline const struct clock_identity *
msg_hdr_get_src_port_id_clock_id(const struct msg_header_wire *hdr)
{
	return (struct clock_identity *)hdr->spid.cid;
}

static inline uint16_t
msg_hdr_get_src_port_id_port_no(const struct msg_header_wire *hdr)
{
	return ntohs(hdr->spid.pn);
}

static inline const uint8_t *
msg_hdr_get_flags(const struct msg_header_wire *hdr)
{
	return hdr->flags;
}

static inline void msg_hdr_init(struct msg_header_wire *hdr,
				struct pp_instance *ppi,
				uint8_t flags[2],
				uint8_t log_msg_intvl)
{
	memset(hdr, 0, sizeof(*hdr));
	hdr->ptp_version = ppi->portDS->versionNumber;
	hdr->domain = ppi->glbs->defaultDS->domainNumber;
	hdr->flags[0] = flags[0]; hdr->flags[1] = flags[1];
	msg_hdr_set_src_port_id(hdr, &ppi->portDS->portIdentity);
	hdr->log_msg_intvl = log_msg_intvl;
}

static inline void msg_hdr_prepare(struct msg_header_wire *hdr,
				   uint8_t type,
				   uint16_t length,
				   uint16_t seq_id,
				   uint8_t ctrl,
				   uint8_t log_msg_intvl)
{
	/* changes in header */
	hdr->ts_mt &= 0xf0;
	/* RAZ messageType */
	hdr->ts_mt |= type;

	hdr->msg_length = htons(length);
	hdr->seq_id = seq_id;
	hdr->ctrl = ctrl;
	hdr->log_msg_intvl = log_msg_intvl;
}


#endif /* __IEEE1588_TYPES_HELPERS_H__ */
