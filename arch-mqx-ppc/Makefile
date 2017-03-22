
CONFIG_CROSS_COMPILE=\
 C:/Freescale/CW_MCU_v10.6.4/MCU/PA_Tools/Command_Line_Tools/

CW_ROOT_DIR=C:/Projects/cw10
MQX_DIR=C:/Projects/mqx_4.0

# globals for MetroWerks IDE $@.args file
export PA_SUPPORT=${CW_ROOT_DIR}/PA_Support
export PA_INCLUDE="${PA_SUPPORT}/ewl/EWL_C/include"
export PA_RUNTIME="${PA_SUPPORT}/ewl/EWL_Runtime/Runtime_PA"
export     CONFIG="${MQX_DIR}/config"
export      DEBUG="${MQX_DIR}/lib/ppxs30m60.cw10/Debug"
export       PPSI="c:/Projects/ppsi/ppsi"
#export       PPSI="c:/Projects/ppsi/ppsi/arch-mqx-ppc"


CC=${CONFIG_CROSS_COMPILE}mwcceppc
CFLAGS=@@mwopt_eppc_com @@mwopt_eppc_cc -MDfile ${@:.o=_c.d}

LD=${CONFIG_CROSS_COMPILE}mwldeppc
LFLAGS=-library @mwopt_epcc_com @@mwopt_eppc_ld

all: jx.o
#ppsi.a:	jx.o
#	${LD} ${LFLAGS} -o $@

