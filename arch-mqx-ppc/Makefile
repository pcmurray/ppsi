# arch-mqx-ppc/Makefile - make PPSi LGPL library with Freescale/MetroWerks tools
# 	arch-wrpc/Makefile is useful ref...

A := arch-$(ARCH)

OBJ-y += \
	$A/jx.o

$(OBJ-y):	$(wildcard $A/include/*.h $A/include/arch/*.h)


#FIX-ME debug junk
#VARS_OLD := $(.VARIABLES)
#CUR-DIR := $(shell pwd)
#LOG-DIR := $(CUR-DIR)/make-logs
#$(foreach v,										\
#  $(filter-out $(VARS_OLD) VARS_OLD,$(.VARIABLES)), \
#  $(info $(v) = $($(v))))


CW_ROOT_DIR:=C:/Projects/cw10
MQX_DIR:=C:/Projects/mqx_4.0

# globals for MetroWerks IDE $@.args file
export PA_SUPPORT:=${CW_ROOT_DIR}/PA_Support
export PA_INCLUDE:=${PA_SUPPORT}/ewl/EWL_C/include
export PA_RUNTIME:=${PA_SUPPORT}/ewl/EWL_Runtime/Runtime_PA
#export     CONFIG="${MQX_DIR}/config"
#export      DEBUG="${MQX_DIR}/lib/ppxs30m60.cw10/Debug"
#export       PPSI="c:/Projects/ppsi/ppsi"



# use MQX printf
CONFIG_NO_PRINTF = y


# mw-specific toolchain defs
mkfile_dir := $(dir $(lastword $(MAKEFILE_LIST)))

CC=${CONFIG_CROSS_COMPILE}mwcceppc

CFLAGS=@@${mkfile_dir}mwopt_eppc_com \
	@@${mkfile_dir}mwopt_eppc_cc

CFLAGS += $(USER_CFLAGS)
CFLAGS += -O$(CONFIG_OPTIMIZATION)
CFLAGS += -Iinclude
CFLAGS += -common off # equiv of gcc -fno-common
CFLAGS += -DPPSI_VERSION=\"$(VERSION)\"



##############################################################
#
# normal arch-*/Makefile assignments
#
CFLAGS += -Itools -Iproto-standard
CFLAGS += -I${mkfile_dir}

# xxx - need time-mqx-eppc?
#TIME = mqx-eppc
#CFLAGS += -Itime-($TIME)
#include time-$(TIME)/Makefile



LD=${CONFIG_CROSS_COMPILE}mwldeppc

LFLAGS=-library @${mkfile_dir}/mwopt_eppc_com \
	@@${mkfile_dir}/mwopt_eppc_ld



all: $(TARGET)

# implicit rules to preprocess c file
#PREPROCESS.c = $(CC) -E $(CFLAGS) $(CPPFLAGS)
#%.i: %.c
#	$(PREPROCESS.c) ${@:.i=.c} > $@


## implicit rules to preprocess c file
#PREPROCESS.c = $(CC) -E $(CFLAGS) $(CPPFLAGS)
#%.i: ${mkfile_dir}mwopt_eppc_i %.c
#	$(subst mwopt_eppc_cc,mwopt_eppc_i,$(PREPROCESS.c)) ${@:.i=.c} > $@
#
#
#${mkfile_dir}mwopt_eppc_i:
#	envsubst >$@ <${mkfile_dir}mwopt_eppc_cc
