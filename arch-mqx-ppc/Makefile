# arch-mqx-ppc/Makefile - make PPSi LGPL library with Freescale/MetroWerks tools
# 	arch-wrpc/Makefile is useful ref...

#FIX-ME debug junk
#VARS_OLD := $(.VARIABLES)
CUR-DIR := $(shell pwd)
LOG-DIR := $(CUR-DIR)/make-logs
$(foreach v,										\
  $(filter-out $(VARS_OLD) VARS_OLD,$(.VARIABLES)), \
  $(info $(v) = $($(v))))



CW_ROOT_DIR=C:/Projects/cw10
MQX_DIR=C:/Projects/mqx_4.0

# globals for MetroWerks IDE $@.args file
export PA_SUPPORT=${CW_ROOT_DIR}/PA_Support
export PA_INCLUDE="${PA_SUPPORT}/ewl/EWL_C/include"
export PA_RUNTIME="${PA_SUPPORT}/ewl/EWL_Runtime/Runtime_PA"
#export     CONFIG="${MQX_DIR}/config"
#export      DEBUG="${MQX_DIR}/lib/ppxs30m60.cw10/Debug"
#export       PPSI="c:/Projects/ppsi/ppsi"


# use MQX printf
CONFIG_NO_PRINTF = y


# mw-specific toolchain defs
mkfile_dir := $(dir $(lastword $(MAKEFILE_LIST)))

CC=${CONFIG_CROSS_COMPILE}mwcceppc

CFLAGS=@@${mkfile_dir}mwopt_eppc_com \
	@@${mkfile_dir}mwopt_eppc_cc \
	-MDfile ${@:.o=_c.d}
CFLAGS += $(USER_CFLAGS)


CFLAGS += -O$(CONFIG_OPTIMIZATION)
CFLAGS += -Iinclude
# -fno-common equiv is -common off - disable tenative declarations
CFLAGS += -common off
CFLAGS += -DPPSI_VERSION=\"$(VERSION)\"


##############################################################
#
# normal arch-*/Makefile assignments
#
CFLAGS += -Itools
CFLAGS += -Iproto-standard

# xxx - need time-mqx-eppc?
#TIME = mqx-eppc
#CFLAGS += -Itime-($TIME)
#include time-$(TIME)/Makefile



LD=${CONFIG_CROSS_COMPILE}mwldeppc

LFLAGS=-library @${mkfile_dir}/mwopt_epcc_com \
	@@${mkfile_dir}/mwopt_eppc_ld



#*.i:
#	${CC} -P @@mwopt_eppc_com @@mwopt_eppc_cc ${@:.i=.c}


all: $(TARGET)
