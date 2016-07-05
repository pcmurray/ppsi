/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#ifndef __PPSI_PPSI_H__
#define __PPSI_PPSI_H__
#include <generated/autoconf.h>


#ifdef CONFIG_E2E
#  define HAS_E2E 1
#else
#  define HAS_E2E 0
#endif
#ifdef CONFIG_P2P
#  define HAS_P2P 1
#else
#  define HAS_P2P 0
#endif

#include <stdint.h>
#include <stdarg.h>
#include <stddef.h>
#include <ppsi/lib.h>
#include <ppsi/ieee1588_types.h>
#include <ppsi/constants.h>
#include <ppsi/jiffies.h>

#include <ppsi/pp-instance.h>
#include <ppsi/diag-macros.h>

#include <arch/arch.h> /* ntohs and so on -- and wr-api.h for wr archs */
#include <ppsi/ieee1588_types_helpers.h>

/* At this point in time, we need ARRAY_SIZE to conditionally build vlan code */
#undef ARRAY_SIZE
#define ARRAY_SIZE(arr) (sizeof(arr) / sizeof((arr)[0]))

/* We can't include pp-printf.h when building freestading, so have it here */
extern int pp_printf(const char *fmt, ...)
	__attribute__((format(printf, 1, 2)));
extern int pp_vprintf(const char *fmt, va_list args)
	__attribute__((format(printf, 1, 0)));
extern int pp_sprintf(char *s, const char *fmt, ...)
	__attribute__((format(printf,2,3)));
extern int pp_vsprintf(char *buf, const char *, va_list)
	__attribute__ ((format (printf, 2, 0)));

/* This structure is never defined, it seems */
struct pp_vlanhdr {
	uint8_t h_dest[6];
	uint8_t h_source[6];
	uint16_t h_tpid;
	uint16_t h_tci;
	uint16_t h_proto;
};

/* Helpers for the fsm (fsm-lib.c) */
extern int pp_lib_may_issue_sync(struct pp_instance *ppi);
extern int pp_lib_may_issue_announce(struct pp_instance *ppi);
extern int pp_lib_may_issue_request(struct pp_instance *ppi);

/* We use data sets a lot, so have these helpers */
static inline struct pp_globals *GLBS(struct pp_instance *ppi)
{
	return ppi->glbs;
}

static inline struct pp_instance *INST(struct pp_globals *ppg,
							int n_instance)
{
	return ppg->pp_instances + n_instance;
}

static inline struct pp_runtime_opts *GOPTS(struct pp_globals *ppg)
{
	return ppg->rt_opts;
}

static inline struct pp_runtime_opts *OPTS(struct pp_instance *ppi)
{
	return GOPTS(GLBS(ppi));
}

static inline struct DSDefault *GDSDEF(struct pp_globals *ppg)
{
	return ppg->defaultDS;
}

static inline struct DSDefault *DSDEF(struct pp_instance *ppi)
{
	return GDSDEF(GLBS(ppi));
}

static inline struct DSCurrent *DSCUR(struct pp_instance *ppi)
{
	return GLBS(ppi)->currentDS;
}

static inline struct DSParent *DSPAR(struct pp_instance *ppi)
{
	return GLBS(ppi)->parentDS;
}

static inline struct DSPort *DSPOR(struct pp_instance *ppi)
{
	return ppi->portDS;
}

static inline struct DSTimeProperties *DSPRO(struct pp_instance *ppi)
{
	return GLBS(ppi)->timePropertiesDS;
}

/* We used to have a "netpath" structure. Keep this until we merge pdelay */
static struct pp_instance *NP(struct pp_instance *ppi)
	__attribute__((deprecated));

static inline struct pp_instance *NP(struct pp_instance *ppi)
{
	return ppi;
}

static inline struct pp_servo *SRV(struct pp_instance *ppi)
{
	return GLBS(ppi)->servo;
}

extern void pp_prepare_pointers(struct pp_instance *ppi);

/*
 * Each extension should fill this structure that is used to augment
 * the standard states and avoid code duplications. Please remember
 * that proto-standard functions are picked as a fall-back when non
 * extension-specific code is provided. The set of hooks here is designed
 * based on what White Rabbit does. If you add more please remember to
 * allow NULL pointers.
 */
struct pp_ext_hooks {
	int (*init)(struct pp_instance *ppg, void *pkt, int plen);
	int (*open)(struct pp_globals *ppi, struct pp_runtime_opts *rt_opts);
	int (*close)(struct pp_globals *ppg);
	int (*listening)(struct pp_instance *ppi, void *pkt, int plen);
	int (*master_msg)(struct pp_instance *ppi, void *pkt,
			  int plen, int msgtype);
	int (*new_slave)(struct pp_instance *ppi, void *pkt, int plen);
	int (*handle_resp)(struct pp_instance *ppi);
	void (*s1)(struct pp_instance *ppi, struct pp_frgn_master *m);
	int (*execute_slave)(struct pp_instance *ppi);
	void (*handle_announce)(struct pp_instance *ppi,
				struct pp_frgn_master *m,
				struct msg_announce_wire *ann);
	int (*handle_followup)(struct pp_instance *ppi, TimeInternal *orig,
			       TimeInternal *correction_field);
	int (*handle_preq) (struct pp_instance * ppi);
	int (*handle_presp)(struct pp_instance *ppi);
	int (*handle_presp_followup) (struct pp_instance * ppi);
	int (*pack_announce)(struct pp_instance *ppi);
};

extern struct pp_ext_hooks pp_hooks; /* The one for the extension we build */


/*
 * Network methods are encapsulated in a structure, so each arch only needs
 * to provide that structure. This simplifies management overall.
 */
struct pp_network_operations {
	int (*init)(struct pp_instance *ppi);
	int (*exit)(struct pp_instance *ppi);
	int (*recv)(struct pp_instance *ppi, void *pkt, int len,
		    TimeInternal *t);
	/* chtype here is PP_NP_GEN or PP_NP_EVT -- use_pdelay must be 0 */
	int (*send)(struct pp_instance *ppi, void *pkt, int len,
		    TimeInternal *t, int chtype, int use_pdelay_addr);
	int (*check_packet)(struct pp_globals *ppg, int delay_ms);
};

/* This is the struct pp_network_operations to be provided by time- dir */
extern struct pp_network_operations DEFAULT_NET_OPS;

/* These can be liked and used as fallback by a different timing engine */
extern struct pp_network_operations unix_net_ops;


/*
 * Time operations, like network operations above, are encapsulated.
 * They may live in their own time-<name> subdirectory.
 *
 * If "set" receives a NULL time value, it should update the TAI offset.
 */
struct pp_time_operations {
	int (*get)(struct pp_instance *ppi, TimeInternal *t);
	int (*set)(struct pp_instance *ppi, TimeInternal *t);
	/* freq_ppb is parts per billion */
	int (*adjust)(struct pp_instance *ppi, long offset_ns, long freq_ppb);
	int (*adjust_offset)(struct pp_instance *ppi, long offset_ns);
	int (*adjust_freq)(struct pp_instance *ppi, long freq_ppb);
	int (*init_servo)(struct pp_instance *ppi);
	unsigned long (*calc_timeout)(struct pp_instance *ppi, int millisec);
};

/* This is the struct pp_time_operations to be provided by time- dir */
extern struct pp_time_operations DEFAULT_TIME_OPS;

/* These can be liked and used as fallback by a different timing engine */
extern struct pp_time_operations unix_time_ops;


/* FIXME this define is no more used; check whether it should be
 * introduced again */
#define  PP_ADJ_NS_MAX		(500*1000)

/* FIXME Restored to value of ptpd. What does this stand for, exactly? */
#define  PP_ADJ_FREQ_MAX	512000

/*
 * Timeouts.
 *
 * A timeout, is just a number that must be compared with the current counter.
 * So we don't need struct operations, as it is one function only,
 * which is folded into the "pp_time_operations" above.
 */
extern void pp_timeout_init(struct pp_instance *ppi);
extern void __pp_timeout_set(struct pp_instance *ppi, int index, int millisec);
extern void pp_timeout_set(struct pp_instance *ppi, int index);
extern void pp_timeout_setall(struct pp_instance *ppi);
extern int pp_timeout(struct pp_instance *ppi, int index)
	__attribute__((warn_unused_result));
extern int pp_next_delay_1(struct pp_instance *ppi, int i1);
extern int pp_next_delay_2(struct pp_instance *ppi, int i1, int i2);
extern int pp_next_delay_3(struct pp_instance *ppi, int i1, int i2, int i3);

/* The channel for an instance must be created and possibly destroyed. */
extern int pp_init_globals(struct pp_globals *ppg, struct pp_runtime_opts *opts);
extern int pp_close_globals(struct pp_globals *ppg);

extern int pp_parse_cmdline(struct pp_globals *ppg, int argc, char **argv);

/* platform independent timespec-like data structure */
struct pp_cfg_time {
	long tv_sec;
	long tv_nsec;
};

/* Data structure used to pass just a single argument to configuration
 * functions. Any future new type for any new configuration function can be just
 * added inside here, without redefining cfg_handler prototype */
union pp_cfg_arg {
	int i;
	int i2[2];
	char *s;
	struct pp_cfg_time ts;
};

/*
 * Configuration: we are structure-based, and a typedef simplifies things
 */
struct pp_argline;

typedef int (*cfg_handler)(struct pp_argline *l, int lineno,
			   struct pp_globals *ppg, union pp_cfg_arg *arg);

struct pp_argname {
	char *name;
	int value;
};
enum pp_argtype {
	ARG_NONE,
	ARG_INT,
	ARG_INT2,
	ARG_STR,
	ARG_NAMES,
	ARG_TIME,
};
struct pp_argline {
	cfg_handler f;
	char *keyword;	/* Each line starts with a keyword */
	enum pp_argtype t;
	struct pp_argname *args;
	size_t field_offset;
	int needs_port;
};

/* Below are macros for setting up pp_argline arrays */
#define OFFS(s,f) offsetof(struct s, f)

#define OPTION(s,func,k,typ,a,field,i)					\
	{								\
		.f = func,						\
		.keyword = k,						\
		.t = typ,						\
		.args = a,						\
		.field_offset = OFFS(s,field),				\
		.needs_port = i,					\
	}

#define LEGACY_OPTION(func,k,typ)					\
	{								\
		.f = func,						\
		.keyword = k,						\
		.t = typ,						\
	}

#define INST_OPTION(func,k,t,a,field)					\
	OPTION(pp_instance,func,k,t,a,field,1)

#define INST_OPTION_INT(k,t,a,field)					\
	INST_OPTION(f_simple_int,k,t,a,field)

#define RT_OPTION(func,k,t,a,field)					\
	OPTION(pp_runtime_opts,func,k,t,a,field,0)

#define GLOB_OPTION(func,k,t,a,field)					\
	OPTION(pp_globals,func,k,t,a,field,0)

#define RT_OPTION_INT(k,t,a,field)					\
	RT_OPTION(f_simple_int,k,t,a,field)

#define GLOB_OPTION_INT(k,t,a,field)					\
	GLOB_OPTION(f_simple_int,k,t,a,field)

/* Both the architecture and the extension can provide config arguments */
extern struct pp_argline pp_arch_arglines[];
extern struct pp_argline pp_ext_arglines[];

/* Note: config_string modifies the string it receives */
extern int pp_config_string(struct pp_globals *ppg, char *s);
extern int pp_config_file(struct pp_globals *ppg, int force, char *fname);
extern int f_simple_int(struct pp_argline *l, int lineno,
			struct pp_globals *ppg, union pp_cfg_arg *arg);

#define PPSI_PROTO_RAW		0
#define PPSI_PROTO_UDP		1
#define PPSI_PROTO_VLAN		2	/* Actually: vlan over raw eth */

#define PPSI_ROLE_AUTO		0
#define PPSI_ROLE_MASTER	1
#define PPSI_ROLE_SLAVE		2

#define PPSI_EXT_NONE		0
#define PPSI_EXT_WR		1


/* Servo */
extern void pp_servo_init(struct pp_instance *ppi);
extern void pp_servo_got_sync(struct pp_instance *ppi); /* got t1 and t2 */
extern void pp_servo_got_resp(struct pp_instance *ppi); /* got all t1..t4 */
extern void pp_servo_got_psync(struct pp_instance *ppi); /* got t1 and t2 */
extern void pp_servo_got_presp(struct pp_instance *ppi); /* got all t3..t6 */

/* bmc.c */
extern void m1(struct pp_instance *ppi);
extern int bmc(struct pp_instance *ppi);

/* msg.c */
extern void msg_pack_header(struct pp_instance *ppi, void *buf);
extern int __attribute__((warn_unused_result))
	msg_unpack_header(struct pp_instance *ppi, void *buf, int plen);
extern void msg_unpack_sync(void *buf, MsgSync *sync);
extern void msg_unpack_follow_up(void *buf, MsgFollowUp *flwup);
extern void msg_unpack_delay_req(void *buf, MsgDelayReq *delay_req);
extern void msg_unpack_delay_resp(void *buf, MsgDelayResp *resp);
/* pdelay */
extern void msg_unpack_pdelay_resp_follow_up(void *buf,
					     MsgPDelayRespFollowUp *
					     pdelay_resp_flwup);
extern void msg_pack_pdelay_resp_follow_up(struct pp_instance *ppi,
					   struct msg_header_wire * hdr,
					   Timestamp * prec_orig_tstamp);
extern void msg_unpack_pdelay_resp(void *buf, MsgPDelayResp * presp);
extern void msg_pack_pdelay_resp(struct pp_instance *ppi,
				 struct msg_header_wire * hdr,
				 Timestamp * rcv_tstamp);
extern void msg_unpack_pdelay_req(void *buf, MsgPDelayReq * pdelay_req);
extern void msg_pack_pdelay_req(struct pp_instance *ppi,
				Timestamp * orig_tstamp);

/* each of them returns 0 if ok, -1 in case of error in send, 1 if stamp err */
#define PP_SEND_OK		0
#define PP_SEND_ERROR		-1
#define PP_SEND_NO_STAMP	1

extern int msg_issue_announce(struct pp_instance *ppi);
extern int msg_issue_sync_followup(struct pp_instance *ppi);
extern int msg_issue_request(struct pp_instance *ppi);
extern int msg_issue_delay_resp(struct pp_instance *ppi, TimeInternal *time);
extern int msg_issue_pdelay_resp_followup(struct pp_instance *ppi,
					  TimeInternal * time);
extern int msg_issue_pdelay_resp(struct pp_instance *ppi, TimeInternal * time);

/* Functions for timestamp handling (internal to protocol format conversion*/
/* FIXME: add prefix in function name? */
extern void cField_to_TimeInternal(TimeInternal *internal, int64_t bigint);
extern int from_TimeInternal(TimeInternal *src, Timestamp *dst);
extern int to_TimeInternal(TimeInternal *dst, Timestamp *src);
extern void add_TimeInternal(TimeInternal *r, TimeInternal *x, TimeInternal *y);
extern void sub_TimeInternal(TimeInternal *r, TimeInternal *x, TimeInternal *y);
extern void div2_TimeInternal(TimeInternal *r);

/*
 * The state machine itself is an array of these structures.
 */

/* Use a typedef, to avoid long prototypes */
typedef int pp_action(struct pp_instance *ppi, void *packet, int plen);

struct pp_state_table_item {
	int state;
	char *name;
	pp_action *f1;
};

extern struct pp_state_table_item pp_state_table[]; /* 0-terminated */

/* Standard state-machine functions */
extern pp_action pp_initializing, pp_faulty, pp_disabled, pp_listening,
		 pp_pre_master, pp_master, pp_passive, pp_uncalibrated,
		 pp_slave, pp_pclock;;

/*
 * MASTER and PRE_MASTER have many things in common. This function implements
 * both states. It is invoked by pp_master and pp_pre_master with pre
 * respectively equal to 0 and 1.
 */
extern int _pp_master(struct pp_instance *ppi, void *pkt, int plen, int pre);

/* The engine */
extern int pp_state_machine(struct pp_instance *ppi, uint8_t *packet, int plen);

/* Frame-drop support -- rx before tx, alphabetically */
extern void ppsi_drop_init(struct pp_globals *ppg, unsigned long seed);
extern int ppsi_drop_rx(void);
extern int ppsi_drop_tx(void);

/* Return !0 in case message is from current parent */
static inline int msg_is_from_current_parent(struct pp_instance *ppi,
					     struct msg_header_wire *hdr)
{
	struct port_identity pid;

	msg_hdr_get_src_port_id(&pid, hdr);
	return (!port_id_cmp(&DSPAR(ppi)->parentPortIdentity, &pid));
}

static inline int resp_is_mine(struct pp_instance *ppi,
			       struct port_identity *pi,
			       struct msg_header_wire *hdr,
			       int index)
{
	return !(port_id_cmp(&DSPOR(ppi)->portIdentity, pi)) &&
		 ppi->sent_seq[index] == msg_hdr_get_msg_seq_id(hdr) &&
		msg_is_from_current_parent(ppi, hdr);
}

#define pdelay_resp_is_mine(ppi, hdr, pi) \
	resp_is_mine(ppi, pi, hdr, PPM_PDELAY_REQ)

#define delay_resp_is_mine(ppi, hdr, pi) \
	resp_is_mine(ppi, pi, hdr, PPM_DELAY_REQ)

#define pdelay_resp_follow_up_is_mine(ppi, hdr, pi) \
	resp_is_mine(ppi, pi, hdr, PPM_PDELAY_REQ)

#define delay_resp_follow_up_is_mine(ppi, hdr, pi) \
	resp_is_mine(ppi, pi, hdr, PPM_DELAY_REQ)

#endif /* __PPSI_PPSI_H__ */
