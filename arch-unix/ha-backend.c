#include <time.h>
#include <ppsi/ppsi.h>
#include "ppsi-unix.h"

static time_t locking_ena_time;

int fake_locking_enable(struct pp_instance *ppi)
{
	pp_diag(ppi, ext, 1, "wrop: %s\n", __func__);
	time(&locking_ena_time);
	return 0; /* ok */
}

int fake_locking_poll(struct pp_instance *ppi, int grandmaster)
{
	time_t now;
	int ret;

	time(&now);
	if (locking_ena_time && (now - locking_ena_time > 3))
		ret = WR_SPLL_READY;
	else
		ret = WR_SPLL_ERROR;
	pp_diag(ppi, ext, 1, "wrop: %s (%li %li) -> %s\n", __func__,
		now, locking_ena_time, ret == WR_SPLL_READY ? "OK" : "ko");
	return ret;
}

int fake_locking_disable(struct pp_instance *ppi)
{
	pp_diag(ppi, ext, 1, "wrop: %s\n", __func__);
	locking_ena_time = 0;
	return 0; /* ok */
}

int fake_enable_ptracker(struct pp_instance *ppi)
{
	pp_diag(ppi, ext, 1, "wrop: %s\n", __func__);
	return 0; /* ok */
}

int fake_adjust_in_progress(void)
{
	pp_diag(NULL, ext, 1, "wrop: %s\n", __func__);
	return 0; /* not in progress */
}

int fake_adjust_counters(int64_t adjust_sec, int32_t adjust_nsec)
{
	return 0; /* ok */
}

int fake_adjust_phase(int32_t phase_ps)
{
	pp_diag(NULL, ext, 1, "wrop: %s (ps: %i)\n", __func__, phase_ps);
	return 0; /* ok */
}

int fake_read_calibration_data(struct pp_instance *ppi,
                              uint32_t *delta_tx, uint32_t *delta_rx,
                              int32_t *fix_alpha, int32_t *clock_period)
{
	if (delta_tx)
		*delta_tx = 1000 * 1000; /* 1 usec */
	if (delta_rx)
		*delta_rx = 1000 * 1000; /* 1 usec */
	if (fix_alpha)
		*fix_alpha = 0;
	if (clock_period)
		*clock_period = 200 * 1000; /* 200ns, fake as usual */
	pp_diag(ppi, ext, 1, "wrop: %s\n", __func__);
	return WR_HW_CALIB_OK;
}

int fake_calibrating_disable(struct pp_instance *ppi, int txrx)
{
	pp_diag(ppi, ext, 1, "wrop: %s\n", __func__);
	return 0; /* ok */
}

int fake_calibrating_enable(struct pp_instance *ppi, int txrx)
{
	pp_diag(ppi, ext, 1, "wrop: %s\n", __func__);
	return 0; /* ok */
}

int fake_calibrating_poll(struct pp_instance *ppi, int txrx, uint32_t *delta)
{
	pp_diag(ppi, ext, 1, "wrop: %s\n", __func__);
	return WR_SPLL_READY;
}

int fake_calibration_pattern_enable(struct pp_instance *ppi,
                                   unsigned int calib_period,
                                   unsigned int calib_pattern,
                                   unsigned int calib_pattern_len)
{
	pp_diag(ppi, ext, 1, "wrop: %s\n", __func__);
	return 0; /* ok */
}

int fake_calibration_pattern_disable(struct pp_instance *ppi)
{
	pp_diag(ppi, ext, 1, "wrop: %s\n", __func__);
	return 0; /* ok */
}

int fake_enable_timing_output(struct pp_instance *ppi, int enable)
{
	pp_diag(ppi, ext, 1, "wrop: %s: %i\n", __func__, enable);
	return 0; /* ok */
}
