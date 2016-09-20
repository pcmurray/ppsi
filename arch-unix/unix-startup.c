/*
 * Copyright (C) 2011 CERN (www.cern.ch)
 * Author: Alessandro Rubini
 *
 * Released to the public domain
 */

/*
 * This is the startup thing for hosted environments. It
 * defines main and then calls the main loop.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/timex.h>
#include <ppsi/ppsi.h>
#include "ppsi-unix.h"

/* ppg and fields */
static struct pp_globals ppg_static;
static DSDefault defaultDS;
static DSCurrent currentDS;
static DSParent parentDS;
static DSTimeProperties timePropertiesDS;
static struct pp_servo servo;

#ifdef CONFIG_EXT_HA
static struct wr_operations fake_wr_operations = {
	.locking_enable = fake_locking_enable,
	.locking_poll = fake_locking_poll,
	.locking_disable = fake_locking_disable,
	.enable_ptracker = fake_enable_ptracker,

	.adjust_in_progress = fake_adjust_in_progress,
	.adjust_counters = fake_adjust_counters,
	.adjust_phase = fake_adjust_phase,

	.read_calib_data = fake_read_calibration_data,
	.calib_disable = fake_calibrating_disable,
	.calib_enable = fake_calibrating_enable,
	.calib_poll = fake_calibrating_poll,
	.calib_pattern_enable = fake_calibration_pattern_enable,
	.calib_pattern_disable = fake_calibration_pattern_disable,

	.enable_timing_output = fake_enable_timing_output,
};
#  define fake_wr_operations_ptr &fake_wr_operations
#else
#  define fake_wr_operations_ptr NULL /* prevent a build-time error later */
#endif

int main(int argc, char **argv)
{
	struct pp_globals *ppg;
	struct pp_instance *ppi;
	unsigned long seed;
	struct timex t;
	int i;

	setbuf(stdout, NULL);

	pp_printf("PPSi. Commit %s, built on " __DATE__ "\n", PPSI_VERSION);

	ppg = &ppg_static;
	ppg->defaultDS = &defaultDS;
	ppg->currentDS = &currentDS;
	ppg->parentDS = &parentDS;
	ppg->timePropertiesDS = &timePropertiesDS;
	ppg->servo = &servo;
	ppg->rt_opts = &__pp_default_rt_opts;

	/* We are hosted, so we can allocate */
	ppg->max_links = PP_MAX_LINKS;
	if (CONFIG_HAS_HA)
		ppg->global_ext_data = calloc(1, sizeof(struct wr_servo_state));

	ppg->arch_data = calloc(1, sizeof(struct unix_arch_data));
	ppg->pp_instances = calloc(ppg->max_links, sizeof(struct pp_instance));

	if ((!ppg->arch_data) || (!ppg->pp_instances)) {
		fprintf(stderr, "ppsi: out of memory\n");
		exit(1);
	}

	/* Before the configuration is parsed, set defaults */
	for (i = 0; i < ppg->max_links; i++) {
		ppi = INST(ppg, i);
		ppi->proto = PP_DEFAULT_PROTO;
		ppi->role = PP_DEFAULT_ROLE;
		ppi->mech = PP_E2E_MECH;
	}

	/* Set offset here, so config parsing can override it */
	if (adjtimex(&t) >= 0)
		timePropertiesDS.currentUtcOffset = t.tai;

	if (pp_parse_cmdline(ppg, argc, argv) != 0)
		return -1;

	/* If no item has been parsed, provide a default file or string */
	if (ppg->cfg.cfg_items == 0)
		pp_config_file(ppg, 0, PP_DEFAULT_CONFIGFILE);
	if (ppg->cfg.cfg_items == 0)
		pp_config_string(ppg, strdup("link 0; iface eth0; proto udp"));

	for (i = 0; i < ppg->nlinks; i++) {

		ppi = INST(ppg, i);
		ppi->ch[PP_NP_EVT].fd = -1;
		ppi->ch[PP_NP_GEN].fd = -1;

		ppi->glbs = ppg;
		ppi->vlans_array_len = CONFIG_VLAN_ARRAY_SIZE,
		ppi->iface_name = ppi->cfg.iface_name;
		ppi->port_name = ppi->cfg.port_name;
		ppi->mech = ppi->cfg.mech;

		/* The following default names depend on TIME= at build time */
		ppi->n_ops = &DEFAULT_NET_OPS;
		ppi->t_ops = &DEFAULT_TIME_OPS;

		ppi->portDS = calloc(1, sizeof(*ppi->portDS));
		ppi->__tx_buffer = malloc(PP_MAX_FRAME_LENGTH);
		ppi->__rx_buffer = malloc(PP_MAX_FRAME_LENGTH);
		if (CONFIG_HAS_HA) {
			void *extds;
			struct wr_dsport *wrp;

			extds = calloc(1, sizeof(struct wr_dsport));
			if (!extds) {
				fprintf(stderr, "ppsi: out of memory\n");
				exit(1);
			}
			ppi->portDS->ext_dsport = extds;
			wrp = WR_DSPOR(ppi); /* just allocated above */
			wrp->ops = fake_wr_operations_ptr;
		}

		if (!ppi->portDS || !ppi->__tx_buffer || !ppi->__rx_buffer) {
			fprintf(stderr, "ppsi: out of memory\n");
			exit(1);
		}
	}
	pp_init_globals(ppg, &__pp_default_rt_opts);

	seed = time(NULL);
	if (getenv("PPSI_DROP_SEED"))
		seed = atoi(getenv("PPSI_DROP_SEED"));
	ppsi_drop_init(ppg, seed);

	unix_main_loop(ppg);
	return 0; /* never reached */
}
