/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 * Based on ptp-noposix project (see AUTHORS for details)
 *
 * Released to the public domain
 */

#include <ppsi/ppsi.h>
#include "wr-api.h"

static inline uint64_t delta_to_scaled_ps(uint32_t delta)
{
	uint64_t d = delta, spm, spl;

	spm = 0xffffffffUL & (d >> 16);
	spl = 0xffffffffUL & (d << 16);
	return (spm << 32) | spl;
}

static inline void print_scaled_ps(struct pp_instance *ppi, const char *s,
				   uint64_t sps)
{
	uint32_t h = sps >> 32;
	uint32_t l = sps & 0xffffffffULL;

	pp_diag(ppi, ext, 1, "%s=>>scaledPicoseconds.msb = 0x%x\n", s, h);
	pp_diag(ppi, ext, 1, "%s=>>scaledPicoseconds.lsb = 0x%x\n", s, l);
}

/*
 * We enter this state from  WRS_M_LOCK or WRS_RESP_CALIB_REQ.
 * We send CALIBRATE and do the hardware steps; finally we send CALIBRATED.
 */
int wr_calibration(struct pp_instance *ppi, void *pkt, int plen)
{
	struct wr_dsport *wrp = WR_DSPOR(ppi);
	int e = 0, sendmsg = 0;
	uint32_t delta;

	if (ppi->is_new_state) {
		wrp->wrStateRetry = WR_STATE_RETRY;
		sendmsg = 1;
	} else if (pp_timeout(ppi, PP_TO_EXT_0)) {
		if (wr_handshake_retry(ppi))
			sendmsg = 1;
		else
			return 0; /* non-wr already */
	}

	if (sendmsg) {
		__pp_timeout_set(ppi, PP_TO_EXT_0, wrp->calPeriod);
		e = msg_issue_wrsig(ppi, CALIBRATE);
		wrp->wrPortState = WR_PORT_CALIBRATION_0;
		if (wrp->calibrated)
			wrp->wrPortState = WR_PORT_CALIBRATION_2;
	}

	pp_diag(ppi, ext, 1, "%s: substate %i\n", __func__,
		wrp->wrPortState - WR_PORT_CALIBRATION_0);

	switch (wrp->wrPortState) {
	case WR_PORT_CALIBRATION_0:
		/* enable pattern sending */
		if (wrp->ops->calib_pattern_enable(ppi, 0, 0, 0) ==
			WR_HW_CALIB_OK)
			wrp->wrPortState = WR_PORT_CALIBRATION_1;
		else
			break;

	case WR_PORT_CALIBRATION_1:
		/* enable Tx calibration */
		if (wrp->ops->calib_enable(ppi, WR_HW_CALIB_TX)
				== WR_HW_CALIB_OK)
			wrp->wrPortState = WR_PORT_CALIBRATION_2;
		else
			break;

	case WR_PORT_CALIBRATION_2:
		/* wait until Tx calibration is finished */
		if (wrp->ops->calib_poll(ppi, WR_HW_CALIB_TX, &delta) ==
			WR_HW_CALIB_READY) {
			wrp->deltaTx.scaledPicoseconds =
				delta_to_scaled_ps(delta);
			print_scaled_ps(ppi, "Tx",
					wrp->deltaTx.scaledPicoseconds);
			wrp->wrPortState = WR_PORT_CALIBRATION_3;
		} else {
			break; /* again */
		}

	case WR_PORT_CALIBRATION_3:
		/* disable Tx calibration */
		if (wrp->ops->calib_disable(ppi, WR_HW_CALIB_TX)
				== WR_HW_CALIB_OK)
			wrp->wrPortState = WR_PORT_CALIBRATION_4;
		else
			break;

	case WR_PORT_CALIBRATION_4:
		/* disable pattern sending */
		if (wrp->ops->calib_pattern_disable(ppi) == WR_HW_CALIB_OK)
			wrp->wrPortState = WR_PORT_CALIBRATION_5;
		else
			break;

	case WR_PORT_CALIBRATION_5:
		/* enable Rx calibration using the pattern sent by other port */
		if (wrp->ops->calib_enable(ppi, WR_HW_CALIB_RX) ==
				WR_HW_CALIB_OK)
			wrp->wrPortState = WR_PORT_CALIBRATION_6;
		else
			break;

	case WR_PORT_CALIBRATION_6:
		/* wait until Rx calibration is finished */
		if (wrp->ops->calib_poll(ppi, WR_HW_CALIB_RX, &delta) ==
			WR_HW_CALIB_READY) {
			pp_diag(ppi, ext, 1, "Rx fixed delay = %d\n", (int)delta);
			wrp->deltaRx.scaledPicoseconds =
				delta_to_scaled_ps(delta);
			print_scaled_ps(ppi, "Rx",
					wrp->deltaRx.scaledPicoseconds);
			wrp->wrPortState = WR_PORT_CALIBRATION_7;
		} else {
			break; /* again */
		}

	case WR_PORT_CALIBRATION_7:
		/* disable Rx calibration */
		if (wrp->ops->calib_disable(ppi, WR_HW_CALIB_RX)
				== WR_HW_CALIB_OK)
			wrp->wrPortState = WR_PORT_CALIBRATION_8;
		else
			break;
	case WR_PORT_CALIBRATION_8:
		/* send deltas to the other port and go to the next state */
		e = msg_issue_wrsig(ppi, CALIBRATED);
		ppi->next_state = WRS_CALIBRATED;
		wrp->calibrated = true;

	default:
		break;
	}

	ppi->next_delay = wrp->wrStateTimeout;

	return e;
}
