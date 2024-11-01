#
# Copyright 2014-2019, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#

#
# src/benchmarks/Makefile -- build all benchmarks
#
TOP := $(dir $(lastword $(MAKEFILE_LIST)))../..

include $(TOP)/src/common.inc

ifeq ($(ANCHOR_FUNCS),1)
vpath %.c $(TOP)/src/examples/libpmemobj/tree_map_anchor
vpath %.c $(TOP)/src/examples/libpmemobj/map_anchor
vpath %.c $(TOP)/src/examples/libpmemobj/hashmap_anchor
else
vpath %.c $(TOP)/src/examples/libpmemobj/tree_map
vpath %.c $(TOP)/src/examples/libpmemobj/map
vpath %.c $(TOP)/src/examples/libpmemobj/hashmap
endif

vpath %.c $(TOP)/src/libpmemobj
vpath %.c $(TOP)/src/common
vpath %.c $(TOP)/src/librpmem
vpath %.c $(TOP)/src/rpmem_common
vpath %.c $(TOP)/src/libpmempool


BENCHMARK = pmembench

GLIB:= $(call check_package, glib-2.0)
ifeq ($(GLIB),y)
TARGET = $(BENCHMARK)
else
$(info NOTE: Skipping pmembench because glib-2.0 is missing \
-- see src/benchmarks/README for details.)
endif

all: $(TARGET)

WORKLOAD_DIR=./workload

SRC=./timers/rdtsc.cpp\
    pmembench.cpp\
    benchmark_time.cpp\
    benchmark_worker.cpp\
    clo.cpp\
    clo_vec.cpp\
    config_reader.cpp\
    scenario.cpp\
    log.cpp\
    blk.cpp\
    pmem_memset.cpp\
    pmem_memcpy.cpp\
    pmem_flush.cpp\
    pmemobj_gen.cpp\
    pmemobj_persist.cpp\
    obj_pmalloc.cpp\
    obj_locks.cpp\
    obj_lanes.cpp\
    pmemobj_tx.cpp\
    pmemobj_atomic_lists.cpp\
    poolset_util.cpp

# Configuration file without the .cfg extension
CONFIGS=pmembench_log\
	pmembench_blk\
	pmembench_memset\
	pmembench_memcpy\
	pmembench_flush\
	pmembench_obj_pmalloc\
	pmembench_obj_persist\
	pmembench_obj_gen\
	pmembench_obj_locks\
	pmembench_obj_lanes\
	pmembench_tx\
	pmembench_atomic_lists

ifeq ($(ANCHOR_FUNCS),1)
SRC+= anchor_map_bench.cpp
CONFIGS+= anchor_pmembench_map
else
SRC+= map_bench.cpp
CONFIGS+= pmembench_map
endif

OBJS=$(SRC:.cpp=.o)
ifneq ($(filter 1 2, $(CSTYLEON)),)
TMP_HEADERS := $(addsuffix tmp, $(HEADERS))
endif
LDFLAGS = -L$(LIBS_PATH) $(OS_LIBS)
ifeq ($(ANCHOR_FUNCS),1)
LDFLAGS += -L../examples/libpmemobj/map_anchor
else
LDFLAGS += -L../examples/libpmemobj/map
endif
LDFLAGS += $(EXTRA_LDFLAGS)


ifeq ($(DEBUG),)
LIBS += ../nondebug/libpmemcommon.a
else
LIBS += ../debug/libpmemcommon.a
endif

CFLAGS += $(LIBNDCTL_CFLAGS)
ifeq ($(ANCHOR_FUNCS),1)
LIBS += -lanchor -lcrypto -lssl
endif
LIBS += -lpmemobj -lpmemlog -lpmemblk -lpmempool -lpmem -pthread -lm \
	$(LIBDL) $(LIBUUID) $(LIBNDCTL_LIBS)
ifeq ($(LIBRT_NEEDED), y)
LIBS += -lrt
endif

CXXFLAGS = -Wall
CXXFLAGS += -Werror
CXXFLAGS += -std=c++11
CXXFLAGS += -Wno-invalid-offsetof
CXXFLAGS += -Wpointer-arith
CXXFLAGS += -Wunused-macros
CXXFLAGS += -pthread
CXXFLAGS += -I../include
CXXFLAGS += -I../libpmemobj
ifeq ($(ANCHOR_FUNCS),1)
CXXFLAGS += -I../anchor
endif

ifeq ($(SCONE),1)
CXXFLAGS += -I./timers
else
ifeq ($(STATISTICS),1)
CXXFLAGS += -I./timers
endif
endif

CXXFLAGS += -I../common
ifeq ($(ANCHOR_FUNCS),1)
CXXFLAGS += -I../examples/libpmemobj/map_anchor
else
CXXFLAGS += -I../examples/libpmemobj/map
endif
CXXFLAGS += -I../rpmem_common
CXXFLAGS += -I../librpmem
CXXFLAGS += $(OS_INCS)
CXXFLAGS += -DSRCVERSION='"$(SRCVERSION)"'

ifeq ($(ANCHOR_FUNCS),1)
CXXFLAGS += -DANCHOR_FUNCS
endif

ifeq ($(SCONE),1)
CXXFLAGS += -DSCONE
endif

ifeq ($(STATISTICS),1)
CXXFLAGS += -DSTATISTICS
ifeq ($(WRITE_AMPL),1)
CXXFLAGS += -DWRITE_AMPL
endif
endif

ifeq ($(GPERF),1)
LIBS+= -lprofiler
LDFLAGS += -L$(GPERF_PATH)/lib
CXXFLAGS += -I$(GPERF_PATH)/include/gperftools -DGPERF
endif

ifeq ($(OS_DIMM),ndctl)
CXXFLAGS += -DSDS_ENABLED
endif

ifeq ($(COVERAGE),1)
CXXFLAGS += $(GCOV_CFLAGS)
LDFLAGS += $(GCOV_LDFLAGS)
LIBS += $(GCOV_LIBS)
endif

ifneq ($(SANITIZE),)
CXXFLAGS += -fsanitize=$(SANITIZE)
LDFLAGS += -fsanitize=$(SANITIZE)
endif

ifeq ($(BUILD_RPMEM),y)
SRC += rpmem.cpp
LIBS += -lrpmem $(shell pkg-config --libs libfabric)
CONFIGS += pmembench_rpmem
CXXFLAGS += -DRPMEM_AVAILABLE
else
$(info NOTE: Skipping librpmem benchmark because $(BUILD_RPMEM_INFO))
endif

ifeq ($(GLIB),y)
CXXFLAGS += $(shell $(PKG_CONFIG) --cflags glib-2.0)
LIBS += $(shell $(PKG_CONFIG) --libs glib-2.0)
endif

ifeq ($(ANCHOR_FUNCS),1)
LIBMAP_DIR=../examples/libpmemobj/map_anchor
LIBMAP+=$(LIBMAP_DIR)/libmap.a
else
LIBMAP_DIR=../examples/libpmemobj/map
LIBMAP=$(LIBMAP_DIR)/libmap.a
endif

OBJS += pmemobj.o $(WORKLOAD_DIR)/generate_traces.o $(WORKLOAD_DIR)/generate_keys.o

#ifeq ($(DEBUG),)
#CXXFLAGS += -O3
#LIBS_PATH=../nondebug
#else
#CXXFLAGS += -ggdb
#LIBS_PATH=../debug
#endif

ifeq ($(DEBUG),1)
CXXFLAGS += -ggdb
LIBS_PATH=../debug
else
CXXFLAGS += -O3
LIBS_PATH=../nondebug
endif

GLIB_TEST_PROG="\#include <glib.h>\nint main(){return 0;}"
GLIB_SILENCE := $(shell printf $(GLIB_TEST_PROG) |\
	$(CXX) $(CXXFLAGS) -x c -o /dev/null - 2>/dev/null && echo n || echo y)

ifeq ($(GLIB_SILENCE), y)
CXXFLAGS += -Wno-unknown-attributes
endif

CXXFLAGS += $(EXTRA_CXXFLAGS)

objdir=.

%.o: %.cpp Makefile
	$(call check-cstyle, $<)
	@mkdir -p .deps
	$(CXX) -MD -c -o $@ $(CXXFLAGS) $(call coverage-path, $<)
	$(call check-os, $@, $<)
	$(create-deps)

./timers/rdtsc.o: ./timers/rdtsc.cpp ./timers/rdtsc.h ./timers/crystal_clock.h
	$(CXX) -MD -c -o $@ $(CXXFLAGS) ./timers/rdtsc.cpp

$(WORKLOAD_DIR):
	$(MAKE) -C $(WORKLOAD_DIR)

%.htmp: %.h
	$(call check-cstyle, $<, $@)

%.hpptmp: %.hpp
	$(call check-cstyle, $<, $@)

$(BENCHMARK): $(WORKLOAD_DIR) $(TMP_HEADERS) $(OBJS) $(LIBMAP)
	$(CXX) -o $@ $(LDFLAGS) $(OBJS) $(LIBMAP) $(LIBS)

$(LIBMAP):
	$(MAKE) -C $(LIBMAP_DIR) map

clean:
	$(RM) $(OBJS) $(TMP_HEADERS)
	$(RM) *.csv
	$(MAKE) -C $(WORKLOAD_DIR) clean

clobber: clean
	$(RM) $(BENCHMARK)
	$(RM) *.csv
	$(RM) -r .deps

$(CONFIGS):
	LD_LIBRARY_PATH=$(LIBS_PATH) ./$(BENCHMARK) $@.cfg > $@.csv

run: $(BENCHMARK) $(CONFIGS)

sparse:

.PHONY: all clean clobber run $(CONFIGS) $(WORKLOAD_DIR)

PMEMOBJ_SYMBOLS=pmalloc pfree lane_hold lane_release

pmemobj.o: $(LIBS_PATH)/libpmemobj/libpmemobj_unscoped.o
	$(OBJCOPY) --localize-hidden $(addprefix -G, $(PMEMOBJ_SYMBOLS)) $< $@

-include .deps/*.P
