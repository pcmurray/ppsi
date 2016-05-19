/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 *
 * Released to the public domain
 */

/* Structs defined in IEEE Std 1588-2008 */

#ifndef __PPSI_IEEE_1588_TYPES_H__
#define __PPSI_IEEE_1588_TYPES_H__

#include <stdint.h>
#include <stdbool.h>

/* See F.2, pag.223 */
#define PP_ETHERTYPE	0x88f7

/* clockAccuracy enumeration (table 6, p. 56) */
enum clk_acc {
	CLOCK_ACCURACY_25NS = 0x20,
	CLOCK_ACCURACY_100NS = 0x21,
	CLOCK_ACCURACY_250NS = 0x22,
	CLOCK_ACCURACY_1US = 0x23,
	CLOCK_ACCURACY_2US5 = 0x24,
	CLOCK_ACCURACY_10US = 0x25,
	CLOCK_ACCURACY_25US = 0x26,
	CLOCK_ACCURACY_100US = 0x27,
	CLOCK_ACCURACY_250US = 0x28,
	CLOCK_ACCURACY_1MS = 0x29,
	CLOCK_ACCURACY_2MS5 = 0x2a,
	CLOCK_ACCURACY_10MS = 0x2b,
	CLOCK_ACCURACY_25MS = 0x2c,
	CLOCK_ACCURACY_100MS = 0x2d,
	CLOCK_ACCURACY_250MS = 0x2e,
	CLOCK_ACCURACY_1S = 0x2f,
	CLOCK_ACCURACY_10S = 0x30,
	CLOCK_ACCURACY_WORST = 0x31,
	CLOCK_ACCURACY_UNKNOWN = 0xfe,
};

/* Enumeration States (table 8, page 73) */
enum pp_std_states {
	PPS_END_OF_TABLE	= 0,
	PPS_INITIALIZING,
	PPS_FAULTY,
	PPS_DISABLED,
	PPS_LISTENING,
	PPS_PRE_MASTER,
	PPS_MASTER,
	PPS_PASSIVE,
	PPS_UNCALIBRATED,
	PPS_SLAVE,
};

enum pp_std_messages {
	PPM_SYNC		= 0x0,
	PPM_DELAY_REQ,
	PPM_PDELAY_REQ,
	PPM_PDELAY_RESP,
	PPM_FOLLOW_UP		= 0x8,
	PPM_DELAY_RESP,
	PPM_PDELAY_RESP_FOLLOW_UP,
	PPM_ANNOUNCE,
	PPM_SIGNALING,
	PPM_MANAGEMENT,
	__PP_NR_MESSAGES_TYPES,

	PPM_NOTHING_TO_DO	= 0x100, /* for hooks.master_msg() */
};

extern const char const * pp_msg_names[];

/* Enumeration Domain Number (table 2, page 41) */
enum ENDomainNumber {
	DFLT_DOMAIN_NUMBER	= 0,
	ALT1_DOMAIN_NUMBER,
	ALT2_DOMAIN_NUMBER,
	ALT3_DOMAIN_NUMBER
};

/* Enumeration Network Protocol (table 3, page 46) */
enum ENNetworkProtocol {
	UDP_IPV4	= 1,
	UDP_IPV6,
	IEEE_802_3,
	DeviceNet,
	ControlNet,
	PROFINET
};

/* Enumeration Time Source (table 7, page 57) */
enum ENTimeSource {
	ATOMIC_CLOCK		= 0x10,
	GPS			= 0x20,
	TERRESTRIAL_RADIO	= 0x30,
	PTP			= 0x40,
	NTP			= 0x50,
	HAND_SET		= 0x60,
	OTHER			= 0x90,
	INTERNAL_OSCILLATOR	= 0xA0
};

/* Enumeration Delay mechanism (table 9, page 74) */
enum ENDelayMechanism {
	E2E		= 1,
	P2P		= 2,
	DELAY_DISABLED	= 0xFE
};

/* tlvType values (table 34, page 154) */
enum ENtlvType {
	MANAGEMENT				= 0x0001,
	MANAGEMENT_ERROR_STATUS			= 0x0002,
	ORGANIZATION_EXTENSION			= 0x0003,
	REQUEST_UNICAST_TRANSMISSION		= 0x0004,
	GRANT_UNICAST_TRANSMISSION		= 0x0005,
	CANCEL_UNICAST_TRANSMISSION		= 0x0006,
	ACKNOWLEDGE_CANCEL_UNICAST_TRASMISSION	= 0x0007,
	PATH_TRACE				= 0x0008,
	ALTERNATE_TIME_OFFSET_INDICATOR		= 0x0009,
	AUTHENTICATION				= 0x2000,
	AUTHENTICATION_CHALLENGE		= 0x2001,
	SECURITY_ASSOCIATION_UPDATE		= 0x2002,
	CUM_FREQ_SCALE_FACTOR_OFFSET		= 0x2003,
};

/* actionField (table 38, page 158) */
enum ENactionField {
	AF_GET		= 0,
	AF_SET		= 1,
	AF_RESPONSE	= 2,
	AF_COMMAND	= 3,
	AF_ACKNOWLEDGE	= 4,
};

/* severityCode (table 46, page 165) */
enum ENseverityCode {
	EMERGENCY	= 0x00,
	ALERT		= 0x01,
	CRITICAL	= 0x02,
	ERROR		= 0x03,
	WARNING		= 0x04,
	NOTICE		= 0x05,
	INFORMATIONAL	= 0x06,
	DEBUG		= 0x07,
};

/* FIXME: each struct must be aligned for lower memory usage */

typedef struct uint48_wire {
	uint16_t	msw;
	uint32_t	lsdw;
} __attribute__((packed)) uint48_wire;

typedef struct uint64_wire {
	uint32_t	msdw;
	uint32_t	lsdw;
} uint64_wire;

typedef struct int64_wire {
	int32_t		msdw;
	uint32_t	lsdw;
} int64_wire;

struct TimeInterval { /* page 12 (32) -- never used */
	int64_t		scaledNanoseconds;
};

/* White Rabbit extension */
typedef struct FixedDelta {
	uint64_t	scaledPicoseconds;
} FixedDelta;

typedef struct Timestamp { /* page 13 (33) -- no typedef expected */
	uint64_t	secondsField;
	uint32_t	nanosecondsField;
} Timestamp;

typedef struct timestamp_wire {
	uint48_wire	secondsField;
	uint32_t	nanosecondsField;
} __attribute__((packed)) timestamp_wire;

typedef struct TimeInternal {
	int32_t		seconds;
	int32_t		nanoseconds;
	/* White Rabbit extension begin */
	int32_t	phase;		/* This is the set point */
	int		correct;	/* 0 or 1 */
#if 0
	/*
	 * The following two fields may be used for diagnostics, but
	 * they cost space. So remove them but keep the code around just
	 * in case it is useful again (they are only set, never read)
	 */
	int32_t		raw_phase;
	int32_t		raw_nsec;
#endif
	int32_t		raw_ahead;  /* raw_ahead is used during calibration */
	/* White Rabbit extension end */
} TimeInternal;

static inline void clear_TimeInternal(struct TimeInternal *t)
{
	memset(t, 0, sizeof(*t));
}

struct clock_identity { /* page 13 (33) */
	uint8_t		id[8];
};
#define PP_CLOCK_IDENTITY_LENGTH	sizeof(struct clock_identity)

struct port_identity { /* page 13 (33) */
	struct clock_identity	clockIdentity;
	uint16_t	portNumber;
};

typedef struct PortAddress { /* page 13 (33) -- never used */
	enum ENNetworkProtocol	networkProtocol;
	uint16_t	adressLength;
	uint8_t		*adressField;
} PortAddress;

struct clock_quality { /* page 14 (34) -- int because of lib/config.c */
	int		clockClass;
	int		clockAccuracy;
	int		offsetScaledLogVariance;
};

struct TLV { /* page 14 (34) -- never used */
	enum ENtlvType	tlvType;
	uint16_t	lengthField;
	uint8_t		*valueField;
};

struct PTPText { /* page 14 (34) -- never used */
	uint8_t		lengthField;
	uint8_t		*textField;
};

struct FaultRecord { /* page 14 (34) -- never used */
	uint16_t	faultRecordLength;
	Timestamp	faultTime;
	enum ENseverityCode	severityCode;
	struct PTPText	faultName;
	struct PTPText	faultValue;
	struct PTPText	faultDescription;
};

/*
 * Message header as encoded on wire, see clause 13.3.1
 */
struct msg_header_wire {
	uint8_t		ts_mt;
	uint8_t		ptp_version;
	uint16_t	msg_length;
	uint8_t		domain;
	uint8_t		resvd1;
	uint8_t		flags[2];
	uint64_wire	cf;
	uint32_t	resvd2;
	struct {
		uint8_t cid[8];
		uint16_t pn;
	} __attribute__((packed)) spid;
	uint16_t	seq_id;
	uint8_t		ctrl;
	uint8_t		log_msg_intvl;
} __attribute__((packed));

struct clock_quality_wire {
	uint8_t clock_class;
	uint8_t clock_accuracy;
	uint16_t offs_slv;
} __attribute__((packed));

struct msg_announce_wire {
	struct msg_header_wire header;
	struct msg_announce_body_wire {
		struct timestamp_wire origin_ts;
		int16_t current_utc_offset;
		uint8_t reserved;
		uint8_t grandmaster_priority1;
		struct clock_quality_wire grandmaster_clock_quality;
		uint8_t grandmaster_priority2;
		struct clock_identity grandmaster_identity;
		uint16_t steps_removed;
		uint8_t time_source;
	} __attribute__((packed)) body;
	struct msg_announce_wrext_wire {
		uint16_t tlv_type;
		uint16_t length;
		uint8_t orgid[3];
		/* Avoid unaligned accesses */
		uint8_t magic[2];
		uint8_t  version;
		/* Avoid unaligned accesses */
		uint8_t wr_msgid[2];
		/* Avoid unaligned accesses */
		uint8_t wr_flags[2];
	} __attribute__((packed)) wrext;
} __attribute__((packed));

/* Sync Message (table 26, page 129) */
typedef struct MsgSync {
	Timestamp originTimestamp;
} MsgSync;

/* DelayReq Message (table 26, page 129) */
typedef struct MsgDelayReq {
	Timestamp	originTimestamp;
} MsgDelayReq;

/* DelayResp Message (table 27, page 130) */
typedef struct MsgFollowUp {
	Timestamp	preciseOriginTimestamp;
} MsgFollowUp;


/* DelayResp Message (table 28, page 130) */
typedef struct MsgDelayResp {
	Timestamp	receiveTimestamp;
	struct port_identity	requestingPortIdentity;
} MsgDelayResp;

/* PdelayReq Message (table 29, page 131) */
typedef struct MsgPDelayReq {
	Timestamp	originTimestamp;
} MsgPDelayReq;

/* PdelayResp Message (table 30, page 131) */
typedef struct MsgPDelayResp {
	Timestamp	requestReceiptTimestamp;
	struct port_identity	requestingPortIdentity;
} MsgPDelayResp;

/* PdelayRespFollowUp Message (table 31, page 132) */
typedef struct MsgPDelayRespFollowUp {
	Timestamp	responseOriginTimestamp;
	struct port_identity	requestingPortIdentity;
} MsgPDelayRespFollowUp;

/* Signaling Message (table 33, page 133) */
typedef struct MsgSignaling {
	struct port_identity	targetPortIdentity;
	char		*tlv;
} MsgSignaling;

/* Management Message (table 37, page 137) - never used */
typedef struct MsgManagement{
	struct port_identity	targetPortIdentity;
	uint8_t		startingBoundaryHops;
	uint8_t		boundaryHops;
	enum ENactionField	actionField;
	char		*tlv;
} MsgManagement;

/* Default Data Set */
typedef struct DSDefault {		/* page 65 */
	/* Static */
	bool		twoStepFlag;
	struct clock_identity	clockIdentity;
	uint16_t	numberPorts;
	/* Dynamic */
	struct clock_quality	clockQuality;
	/* Configurable */
	uint8_t		priority1;
	uint8_t		priority2;
	uint8_t		domainNumber;
	bool		slaveOnly;
} DSDefault;

/* Current Data Set */
typedef struct DSCurrent {		/* page 67 */
	/* Dynamic */
	uint16_t	stepsRemoved;
	TimeInternal	offsetFromMaster;
	TimeInternal	meanPathDelay; /* oneWayDelay */
	/* White Rabbit extension begin */
	uint16_t	primarySlavePortNumber;
	/* White Rabbit extension end */
} DSCurrent;

/* Parent Data Set */
typedef struct DSParent {		/* page 68 */
	/* Dynamic */
	struct port_identity	parentPortIdentity;
	/* bool		parentStats; -- not used */
	uint16_t	observedParentOffsetScaledLogVariance;
	int32_t		observedParentClockPhaseChangeRate;
	struct clock_identity	grandmasterIdentity;
	struct clock_quality	grandmasterClockQuality;
	uint8_t		grandmasterPriority1;
	uint8_t		grandmasterPriority2;
} DSParent;

/* Port Data set */
typedef struct DSPort {			/* page 72 */
	/* Static */
	struct port_identity	portIdentity;
	/* Dynamic */
	/* Enumeration8	portState; -- not used */
	int8_t		logMinDelayReqInterval; /* -- same as pdelay one */
	/* TimeInternal	peerMeanPathDelay; -- not used */
	/* Configurable */
	int8_t	logAnnounceInterval;
	uint8_t		announceReceiptTimeout;
	int8_t	logSyncInterval;
	/* Enumeration8	delayMechanism; -- not used */
	uint8_t		versionNumber;

	void		*ext_dsport;
} DSPort;

/* Time Properties Data Set */
typedef struct DSTimeProperties {	/* page 70 */
	/* Dynamic */
	int16_t		currentUtcOffset;
	uint8_t		flags;
	enum ENTimeSource	timeSource;
} DSTimeProperties;

#endif /* __PPSI_IEEE_1588_TYPES_H__ */
