/*
 * Copyright (C) 2016 CERN (www.cern.ch)
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 */

#ifndef __HAEXT_HA_API_H__
#define __HAEXT_HA_API_H__

#include <ppsi/lib.h>
#include <math.h>
/* We are still very dependent on whiterabbit code, so include wr_ops etc */
#include "../proto-ext-whiterabbit/wr-api.h"

/* Rename the timeouts, for readability */
#define HA_TO_TX	PP_TO_EXT_0
#define HA_TO_RX	PP_TO_EXT_1
#define HA_TO_LOCK	PP_TO_EXT_2

#define HA_DEFAULT_L1SYNCRECEIPTTIMEOUT 5 /* was 3: need more for pll lock */

#define  TLV_TYPE_L1_SYNC		0x8001

#define FIX_ALPHA_FRACBITS 40

/*
 * We don't have ha_dsport, but rather rely on wr_dsport with our fields added.
 *
 *
 * Fortunately (and strangely (thanks Maciej), here the spec *is* consistent.
 * These 3 bits are valid 4 times, in 4 different bytes (conf/active, me/peer)
 */
#define HA_TX_COHERENT	0x01
#define HA_RX_COHERENT	0x02
#define HA_CONGRUENT	0x04
#define HA_OPT_PARAMS	0x08

enum l1_sync_states { /*draft P1588_v_29: page 334 */
	__L1SYNC_MISSING = 0, /* my addition... */ //TODO: std-error->report in ballout
	L1SYNC_DISABLED = 1,
	L1SYNC_IDLE,
	L1SYNC_LINK_ALIVE,
	L1SYNC_CONFIG_MATCH,
	L1SYNC_UP,
};

/* Pack/Unkpack HA messages (all of them are signalling messages) */
int ha_pack_signal(struct pp_instance *ppi);
int ha_unpack_signal(struct pp_instance *ppi, void *pkt, int plen);

/* convert from ps to scaledNanoseconds, ingress and engress latencies
 * - are provided by HW in ps
 * - are stored in scaledNanoseconds, as per standard
 */
static inline TimeInterval ps_to_scaledNs(int32_t ps)
{
	TimeInterval sps; 
	sps.scaledNanoseconds = (int64_t)(((((int64_t)ps) << 16))/1000);
	return sps;
}
/**  ************ this functions probably need some other place...**********************/
/* convert scaledNanoseconds to ps, ingress and engress latencies
 * - are provided by HW (i.e. HAL) in ps
 * - are stored in scaledNanoseconds, as per standard
 */
static inline int32_t scaledNs_to_ps(TimeInterval d)
{
	return (int32_t)((d.scaledNanoseconds*1000) >> 16);
}

/* This will need to be removed for WRPC
 */
static inline double relativeDiff_to_alpha(RelativeDiff rd)
{
	return ((double)rd.scaledRelativeDifference)/(double)pow(2.0, 62.0);
}
/**  **************************************************************************************/

#ifdef CONFIG_USE_HA
/*
 * We rely on wr-api.h and the WR structures, but the setup of the link
 * is completely different. Fortunately, the ugly WR field names are unique,
 * so we can prevent their use in HA code by defining them away. HACK!
 *
 * In practice, we only keep deltaTx/RX, otherNodeDeltaTx/Rx and linkUP.
 * And, well... wrModeOn because all stamps are good in non-ha mode
*/
#define wrConfig		do_not_use_wrConfig
#define wrMode			do_not_use_wrMode
#define wrPortState		do_not_use_wrPortState
#define calibrated		do_not_use_calibrated
#define wrStateTimeout		do_not_use_wrStateTimeout
#define wrStateRetry		do_not_use_wrStateRetry
#define calPeriod		do_not_use_calPeriod
#define calRetry		do_not_use_calRetry
#define parentWrConfig		do_not_use_parentWrConfig
#define parentIsWRnode		do_not_use_parentIsWRnode
#define msgTmpWrMessageID	do_not_use_msgTmpWrMessageID
#define parentWrModeOn		do_not_use_parentWrModeOn
#define parentCalibrated	do_not_use_parentCalibrated
#define otherNodeCalSendPattern	do_not_use_otherNodeCalSendPattern
#define otherNodeCalPeriod	do_not_use_otherNodeCalPeriod
#define otherNodeCalRetry	do_not_use_otherNodeCalRetry
#define doRestart		do_not_use_doRestart
#endif /* CONFIG_USE_HA */


/** *************** new servo functions ********************** */

int wr_delay_ms_cal(struct pp_instance *ppi, struct wr_servo_state *s,
			TimeInternal *ts_offset_hw, int64_t*ts_offset_ps,
			int64_t *delay_ms_fix,  int64_t *fiber_fix_alpha);
int ha_delay_ms_cal(struct pp_instance *ppi, struct wr_servo_state *s,
			TimeInternal *ts_offset_hw, int64_t*ts_offset_ps,
			int64_t *delay_ms_fix, int64_t *fiber_fix_alpha);
void ha_print_correction_values(struct pp_instance *ppi);
int ha_update_correction_values(struct pp_instance *ppi);
uint8_t ha_L1Sync_creat_bitmask(int tx_coh, int rx_coh, int congru);
void ha_print_L1Sync_basic_bitmaps(struct pp_instance *ppi,
			uint8_t configed, uint8_t active, char* text);
#endif /* __HAEXT_HA_API_H__ */
