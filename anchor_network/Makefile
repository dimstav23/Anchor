CXX ?= c++
CXXFLAGS ?= -g -Wall -O2
CXXFLAGS += -Wno-unused-function -Wno-deprecated-declarations

ifeq ($(SCONE),1)
CXXFLAGS += -DSCONE
endif

LIBMAP = ../src/examples/libpmemobj/map_anchor/libmap.a

INCS ?= -I../src/include
INCS += -I../src/examples
INCS += -I../src/libpmem2
INCS += -I../src/examples/libpmemobj/map_anchor
INCS += -I../src/examples/libpmemobj
INCS += -I../network-stack/client_server_twosided/src
INCS += -I../src/benchmarks/workload
INCS += -I../src/benchmarks/timers
INCS += -I../src/anchor/

ifeq ($(SCONE),1)

ifeq ($(RDMA),1)
CXXFLAGS += -DRDMA
INCS +=
else
INCS += -I../anchor-eRPC/eRPC/src
INCS += -I../anchor-eRPC/eRPC/dpdk_scone/x86_64-native-linuxapp-gcc/include
endif

else

ifeq ($(RDMA),1)
CXXFLAGS += -DRDMA
INCS += -I../network-stack/client_server_twosided/eRPC4rdma/src
else
INCS += -I../network-stack/client_server_twosided/eRPC4dpdk/src
INCS += -I../network-stack/client_server_twosided/dpdk_static/include/dpdk
endif

endif

TRACES_OBJ = ../src/benchmarks/workload/generate_traces.o
KEYS_OBJ = ../src/benchmarks/workload/generate_keys.o
TIMERS_OBJ = ../src/benchmarks/timers/rdtsc.o

LD_FLAGS = -L../src/nondebug
LD_FLAGS += -L../network-stack/client_server_twosided/build
RPATH = -Wl,-rpath=../src/nondebug,-rpath=../network-stack/client_server_twosided/build

LIBS = -lanchor -lpmemobj -lssl -lcrypto 
LIBSERVER = -lanchorserver
LIBCLIENT = -lanchorclient

all: server client

server: server.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< ${INCS} ${LIBMAP} ${RPATH} ${KEYS_OBJ} ${LD_FLAGS} ${LIBS} ${LIBSERVER}

client: client.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< ${INCS} ${LIBMAP} ${RPATH} ${TRACES_OBJ} ${TIMERS_OBJ} ${LD_FLAGS} ${LIBCLIENT}


clean:
	rm -rf server client
