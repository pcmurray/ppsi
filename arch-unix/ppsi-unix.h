/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Alessandro Rubini
 *
 * Released to the public domain
 */

/*
 * These are the functions provided by the various unix files
 */

#define POSIX_ARCH(ppg) ((struct unix_arch_data *)(ppg->arch_data))
struct unix_arch_data {
	struct timeval tv;
};

extern void unix_main_loop(struct pp_globals *ppg);

/* Fake HA methods */
int fake_locking_enable(struct pp_instance *ppi);
int fake_locking_poll(struct pp_instance *ppi, int grandmaster);
int fake_locking_disable(struct pp_instance *ppi);
int fake_enable_ptracker(struct pp_instance *ppi);
int fake_adjust_in_progress(void);
int fake_adjust_counters(int64_t adjust_sec, int32_t adjust_nsec);
int fake_adjust_phase(int32_t phase_ps);

int fake_read_calibration_data(struct pp_instance *ppi,
                              uint32_t *delta_tx, uint32_t *delta_rx,
                              int32_t *fix_alpha, int32_t *clock_period);
int fake_calibrating_disable(struct pp_instance *ppi, int txrx);
int fake_calibrating_enable(struct pp_instance *ppi, int txrx);
int fake_calibrating_poll(struct pp_instance *ppi, int txrx, uint32_t *delta);
int fake_calibration_pattern_enable(struct pp_instance *ppi,
                                   unsigned int calib_period,
                                   unsigned int calib_pattern,
                                   unsigned int calib_pattern_len);
int fake_calibration_pattern_disable(struct pp_instance *ppi);

int fake_enable_timing_output(struct pp_instance *ppi, int enable);
