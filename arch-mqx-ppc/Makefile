# arch-mqx-ppc/Makefile - make PPSi LGPL library with Freescale/MetroWerks tools
# 	arch-wrpc/Makefile is useful ref...


CW_ROOT_DIR=C:/Projects/cw10
MQX_DIR=C:/Projects/mqx_4.0

# globals for MetroWerks IDE $@.args file
export PA_SUPPORT=${CW_ROOT_DIR}/PA_Support
export PA_INCLUDE="${PA_SUPPORT}/ewl/EWL_C/include"
export PA_RUNTIME="${PA_SUPPORT}/ewl/EWL_Runtime/Runtime_PA"
export     CONFIG="${MQX_DIR}/config"
export      DEBUG="${MQX_DIR}/lib/ppxs30m60.cw10/Debug"
export       PPSI="c:/Projects/ppsi/ppsi"


# use MQX printf
CONFIG_NO_PRINTF = y


mkfile_dir := $(dir $(lastword $(MAKEFILE_LIST)))

CC=${CONFIG_CROSS_COMPILE}mwcceppc

CFLAGS=@@${mkfile_dir}mwopt_eppc_com \
	@@${mkfile_dir}mwopt_eppc_cc \
	-MDfile ${@:.o=_c.d}

LD=${CONFIG_CROSS_COMPILE}mwldeppc

LFLAGS=-library @${mkfile_dir}/mwopt_epcc_com \
	@@${mkfile_dir}/mwopt_eppc_ld


#
#.PHONY: all tphony
#
#all:
#	echo "${mkfile_path}"
#
#tphony: jx.o
#

jx.i:
	${CC} -P @@mwopt_eppc_com @@mwopt_eppc_cc ${@:.i=.c}

#ppsi.a:	jx.o
#	${LD} ${LFLAGS} -o $@

