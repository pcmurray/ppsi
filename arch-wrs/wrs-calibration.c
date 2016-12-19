/*
 * Copyright (C) 2012 CERN (www.cern.ch)
 * Author: Aurelio Colosimo
 *
 * Released to the public domain
 */

#include <ppsi/ppsi.h>
#include <math.h>

#define HAL_EXPORT_STRUCTURES
#include <ppsi-wrs.h>

#include <hal_exports.h>

int wrs_read_calibration_data(struct pp_instance *ppi,
	uint32_t *delta_tx, uint32_t *delta_rx, int32_t *fix_alpha,
	int32_t *clock_period)
{
	struct hal_port_state *p;
	/* The following fields come from struct hexp_port_state */
	uint32_t port_delta_tx, port_delta_rx;
	int32_t port_fix_alpha;
	
	p = pp_wrs_lookup_port(ppi->iface_name);
	if (!p)
		return WR_HW_CALIB_NOT_FOUND;

	if(!p->calib.tx_calibrated || !p->calib.rx_calibrated)
		return WR_HW_CALIB_NOT_FOUND;

	/*
	 * Like in wrs_net_init, we build fields that were in
	 * hexp_port_state from the "real" hal_port_state in the same
	 * way as the HAL itself was doing to fill the RPC structure.
	 * Formulas copied from libwr/hal_shmem.c (get_exported_state).
	 */
	port_delta_tx = p->calib.delta_tx_phy
		+ p->calib.sfp.delta_tx_ps + p->calib.delta_tx_board;
	port_delta_rx = p->calib.delta_rx_phy
		+ p->calib.sfp.delta_rx_ps + p->calib.delta_rx_board;
	port_fix_alpha =  (double)pow(2.0, 40.0) *
		((p->calib.sfp.alpha + 1.0) / (p->calib.sfp.alpha + 2.0)
		 - 0.5);

	pp_diag(ppi, servo, 1, "deltas: tx=%d, rx=%d\n",
		port_delta_tx, port_delta_rx);

	if(delta_tx)
		*delta_tx = port_delta_tx;
	if(delta_rx)
		*delta_rx = port_delta_rx;
	if(fix_alpha)
		*fix_alpha = port_fix_alpha;
	if(clock_period)
		*clock_period =  16000; /* REF_CLOCK_PERIOD_PS */
	return WR_HW_CALIB_OK;
}

int wrs_read_correction_data(struct pp_instance *ppi, int64_t *delayCoeff,
		int64_t *ingressLatency,   int64_t *egressLatency, 
		int64_t *msgTPointLatency, int64_t *delayAsymmetry,
		int64_t *fixAlpha,         int32_t *clock_period )
{
	/* The following fields come from struct hexp_port_state */
	int64_t port_iL, port_eL, port_dC, port_mTPL, port_dA, port_fix_alpha;
	double alpha;
	int32_t port_cP;
	
	/** TODO: ******* get this values from HAL directly ***************/
	uint32_t delta_tx, delta_rx;
	int32_t fix_alpha;
	wrs_read_calibration_data(ppi, &delta_tx, &delta_rx, &fix_alpha, &port_cP);
	port_iL        = (((int64_t)delta_rx) << 16)/1000;
	port_eL        = (((int64_t)delta_tx) << 16)/1000;
	port_mTPL      = 0;
	port_dA        = 0;
	if(fix_alpha > 0)//to make it more realist
		port_dC =  1235332333756144;
	else
		port_dC = -1235001513901056;
	/** **************************************************************/
	
	alpha          = ((double)port_dC)/(double)pow(2.0, 62.0);
	port_fix_alpha = (double)pow(2.0, 40.0) * ((alpha + 1.0) / (alpha + 2.0) - 0.5);
	
	if(delayCoeff)
		*delayCoeff       = port_dC;
	if(ingressLatency)
		*ingressLatency   = port_iL;
	if(egressLatency)
		*egressLatency    = port_eL;
	if(msgTPointLatency)
		*msgTPointLatency = port_mTPL;
	if(delayAsymmetry)
		*delayAsymmetry   = port_dA;
	if(fixAlpha)
		*fixAlpha         = port_fix_alpha;
	if(clock_period)
		*clock_period     =  port_cP; /* REF_CLOCK_PERIOD_PS */
	return WR_HW_CALIB_OK;

}

int wrs_calibrating_disable(struct pp_instance *ppi, int txrx)
{
	return 0;
}

int wrs_calibrating_enable(struct pp_instance *ppi, int txrx)
{
	return 0;
}

int wrs_calibrating_poll(struct pp_instance *ppi, int txrx, uint32_t *delta)
{
	uint32_t delta_tx, delta_rx;

	wrs_read_calibration_data(ppi, &delta_tx, &delta_rx, NULL, NULL);

	*delta = (txrx == WR_HW_CALIB_TX) ? delta_tx : delta_rx;

	return WR_HW_CALIB_READY;
}

int wrs_calibration_pattern_enable(struct pp_instance *ppi,
				    unsigned int calib_period,
				    unsigned int calib_pattern,
				    unsigned int calib_pattern_len)
{
	return WR_HW_CALIB_OK;
}

int wrs_calibration_pattern_disable(struct pp_instance *ppi)
{
	return WR_HW_CALIB_OK;
}
