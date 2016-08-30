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

static inline uint64_t delta_to_scaled_ps(uint32_t delta)
{
	uint64_t d = delta;

	return d << 16;
}

static inline int32_t scaled_ps_to_delta(uint64_t sps)
{
	return sps >> 16;
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

static inline void msg_hdr_set_msg_dn(struct msg_header_wire *hdr, uint8_t dn)
{
	hdr->domain = dn;
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

static inline const struct clock_identity *
msg_hdr_get_src_clock_id(const struct msg_header_wire *hdr)
{
	return (struct clock_identity *)hdr->spid.cid;
}

static inline uint16_t
msg_hdr_get_src_port_no(const struct msg_header_wire *hdr)
{
	return ntohs(hdr->spid.pn);
}

static inline const uint8_t *
msg_hdr_get_flags(const struct msg_header_wire *hdr)
{
	return hdr->flags;
}

static inline int msg_hdr_get_flag(const struct msg_header_wire *hdr, int pos)
{
	const uint8_t *ptr = &hdr->flags[pos >> 3];
	uint8_t mask = (1 << (pos & 0x7));

	return *ptr & mask;
}

static inline void msg_hdr_set_flags(struct msg_header_wire *hdr,
				     const uint8_t mask[2], const uint8_t v[2])
{
	int i;

	for (i = 0; i < 2; i++) {
		hdr->flags[i] &= ~mask[i];
		hdr->flags[i] |= (v[i] & mask[i]);
	}
}

static inline void msg_hdr_reset_flags(struct msg_header_wire *hdr)
{
	hdr->flags[1] = hdr->flags[0] = 0;
}

static inline void msg_hdr_set_flag(struct msg_header_wire *hdr, int pos, int v)
{
	uint8_t *ptr = &hdr->flags[pos >> 3];
	uint8_t mask = (1 << (pos & 0x7));

	*ptr &= ~mask;
	if (v)
		*ptr |= mask;
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
	hdr->seq_id = htons(seq_id);
	hdr->ctrl = ctrl;
	hdr->log_msg_intvl = log_msg_intvl;
}


static inline struct msg_announce_body_wire *
msg_announce_get_body(struct msg_announce_wire *ann)
{
	return &ann->body;
}

static inline struct msg_header_wire *
msg_announce_get_header(struct msg_announce_wire *ann)
{
	return &ann->header;
}

static inline struct msg_announce_wrext_wire *
msg_announce_get_wrext(struct msg_announce_wire *ann)
{
	if (ntohs(ann->header.msg_length) <= PP_ANNOUNCE_LENGTH)
		/* No white rabbit extensions */
		return NULL;
	return &ann->wrext;
}

static inline uint16_t
msg_announce_get_steps_removed(const struct msg_announce_body_wire *a)
{
	return a->steps_removed;
}

static inline
void msg_announce_get_gm_cid(struct clock_identity *dst,
			     const struct msg_announce_body_wire *src)
{
	*dst = src->grandmaster_identity;
}

static inline struct clock_identity *
msg_announce_get_gm_cid_ptr(struct msg_announce_body_wire *src)
{
	return &src->grandmaster_identity;
}

static inline void
msg_announce_get_gm_cq(struct clock_quality *dst,
		       const struct msg_announce_body_wire *src)
{
	dst->clockClass = src->grandmaster_clock_quality.clock_class;
	dst->clockAccuracy = src->grandmaster_clock_quality.clock_accuracy;
	dst->offsetScaledLogVariance =
		ntohs(src->grandmaster_clock_quality.offs_slv);
}

static inline uint8_t
msg_announce_get_gm_p1(const struct msg_announce_body_wire *src)
{
	return src->grandmaster_priority1;
}

static inline uint8_t
msg_announce_get_gm_p2(const struct msg_announce_body_wire *src)
{
	return src->grandmaster_priority2;
}

static inline uint8_t
msg_announce_get_ts(const struct msg_announce_body_wire *src)
{
	return src->time_source;
}

static inline int16_t
msg_announce_get_utc_offs(const struct msg_announce_body_wire *src)
{
	return src->current_utc_offset;
}

static inline void
msg_announce_set_steps_removed(struct msg_announce_body_wire *dst,
			       uint16_t s)
{
	dst->steps_removed = htons(s);
}

static inline void
msg_announce_set_gm_cid(struct msg_announce_body_wire *dst,
			const struct clock_identity *src)
{
	dst->grandmaster_identity = *src;
}

static inline void
msg_announce_set_gm_cq(struct msg_announce_body_wire *dst,
		       const struct clock_quality *src)
{
	dst->grandmaster_clock_quality.clock_class = src->clockClass;
	dst->grandmaster_clock_quality.clock_accuracy = src->clockAccuracy;
	dst->grandmaster_clock_quality.offs_slv =
		htons(src->offsetScaledLogVariance);
}

static inline void
msg_announce_set_gm_p1(struct msg_announce_body_wire *dst, uint8_t p)
{
	dst->grandmaster_priority1 = p;
}

static inline void
msg_announce_set_gm_p2(struct msg_announce_body_wire *dst, uint8_t p)
{
	dst->grandmaster_priority2 = p;
}

static inline
void msg_announce_set_ts(struct msg_announce_body_wire *dst, uint8_t ts)
{
	dst->time_source = ts;
}

static inline
void msg_announce_set_utc_offs(struct msg_announce_body_wire *dst,
			       uint16_t o)
{
	dst->current_utc_offset = htons(o);
}

static inline void
msg_announce_set_origts(struct msg_announce_body_wire *dst,
			const struct timestamp_wire *origts)
{
	dst->origin_ts = *origts;
}

static inline uint16_t
msg_announce_wr_get_tlv_type(const struct msg_announce_wrext_wire *e)
{
	return ntohs(e->tlv_type);
}

static inline uint32_t
msg_announce_wr_get_tlv_oid(const struct msg_announce_wrext_wire *e)
{
	return (e->orgid[0] << 16) | (e->orgid[1] << 8) | e->orgid[2];
}

static inline uint16_t
msg_announce_wr_get_tlv_magic(const struct msg_announce_wrext_wire *e)
{
	return (e->magic[0] << 8) | e->magic[1];
}

static inline uint16_t
msg_announce_wr_get_tlv_ver(const struct msg_announce_wrext_wire *e)
{
	return ntohs(e->version);
}

static inline uint16_t
msg_announce_wr_get_tlv_mid(const struct msg_announce_wrext_wire *e)
{
	return (e->wr_msgid[0] << 8) | e->wr_msgid[1];
}

static inline const uint8_t *
msg_announce_wr_get_tlv_flags(const struct msg_announce_wrext_wire *e)
{
	return e->wr_flags;
}

#endif /* __IEEE1588_TYPES_HELPERS_H__ */
