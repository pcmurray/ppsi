/*
 * Copyright (C) 2015 CERN (www.cern.ch)
 * Author: Maciej Lipinski
 *
 * Released according to the GNU LGPL, version 2.1 or any later version.
 * 
 */
#include <ppsi/ppsi.h>
#include <ppsi-wrs.h>

#include <stddef.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/ioctl.h>

#include <switch_hw.h>
// #include <hal_client.h>

#include <fpga_io.h>
#include <regs/psu-regs.h>
#include <stddef.h>
#define psu_rd(reg) \
	 _fpga_readl(FPGA_BASE_PSU + offsetof(struct PSU_WB, reg))

#define psu_wr(reg, val) \
	 _fpga_writel(FPGA_BASE_PSU + offsetof(struct PSU_WB, reg), val)

int wrs_psu_init(int hClkCls, int inj_prio)
{
	uint32_t val = 0;
	int err =0;
	err = shw_fpga_mmap_init();
	if(err)
		pp_printf("[PSU] HW access init failed\n");
	val = PSU_PCR_INJ_PRIO_W(inj_prio) | PSU_PCR_HOLDOVER_CLK_CLASS_W(hClkCls) | PSU_PCR_PSU_ENA;
	psu_wr(PCR, val);
	pp_printf("[PSU] initialze: injection priority %d, holdover clock class %d\n", 
	inj_prio, hClkCls);
}
	 
int wrs_psu_add_master_port(int port)
{
	uint32_t val = 0;
	val = psu_rd(TXPM);
	val = val | (0x1 << port);
	psu_wr(TXPM, val);
	pp_printf("[PSU] added master port %d | mask 0x4%x\n", port, val);
}

int wrs_psu_remove_master_port(int port)
{
	uint32_t val = 0;
	val = psu_rd(TXPM);
	val = val & ~(0x1 << port);
	psu_wr(TXPM, val);
	pp_printf("[PSU] removed master port %d | mask 0x4%x\n", port, val);
}

int wrs_psu_set_active_slave_port(int port)
{
	uint32_t val = 0;
	int i, old;
	val = psu_rd(RXPM);
	for(i=0;i<32;i++) if((val >> i) & 0x1) old=i;
	pp_printf( "[PSU] setting slaver port %d | mask 0x4%x\n", port, old);
	val = 0x1 << port;
	psu_wr(RXPM, val);
}
