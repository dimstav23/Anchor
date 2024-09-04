CC=gcc
CFLAGS=-Wall -Werror -std=gnu99

ifeq ($(DEBUG),1)
CFLAGS += -Wp,-U_FORTIFY_SOURCE
CFLAGS += -ggdb
else
CFLAGS += -O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
endif

ifeq ($(ANCHOR_FUNCS),1)
LIBS=-lanchor
INCS+=-I../../include/anchor
endif

ifeq ($(SCONE),1)
CFLAGS += -DSCONE
endif

ifeq ($(STATISTICS),1)
CFLAGS += -DSTATISTICS
endif

INCS += -I. -I../../include -I../../examples #to include the appropriate header files
OBJ_FILES_NAMES = user_operations.o tx_user_operations.o manifest_operations.o hashmap.o openssl_gcm_encrypt.o trusted_counter.o metadata_operations.o metadata_log.o
LIBS += -lpmem -lpmemobj -lcrypto -lssl -lpthread

ifeq ($(DEBUG),1)
LD_LIBRARY_PATH= -L../../debug#to find libpmem and libpmemobj libraries
OBJ_DIR=../../debug/anchor/#debug folder to place the .o files
RPATH=${CURDIR}/../../debug/
EXEC_PATH=./debug/
else
LD_LIBRARY_PATH= -L../../nondebug#to find libpmem and libpmemobj libraries
OBJ_DIR=../../nondebug/anchor/#nondebug folder to place the .o files
RPATH=${CURDIR}/../../nondebug/
EXEC_PATH=./
endif

OBJ_FILES=$(addprefix $(OBJ_DIR),$(OBJ_FILES_NAMES))

ifeq ($(ANCHOR_FUNCS),1)
ifeq ($(WRITE_AMPL),1)
PROGS += extra_writes
CFLAGS += -DWRITE_AMPL
EXEC_NAMES += extra_writes
endif
endif

INCS+=-I../../common

all: $(EXEC_PATH) $(addprefix $(EXEC_PATH),$(PROGS))

$(EXEC_PATH):
	mkdir -p $(EXEC_PATH)

$(EXEC_PATH)extra_writes: $(EXEC_PATH)extra_writes.o
	$(CC) -o $@ $(addprefix $(EXEC_PATH),extra_writes.o) -Wl,-rpath=$(RPATH) $(LIBS) $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH)

$(EXEC_PATH)extra_writes.o: extra_writes.c $(OBJ_FILES)
	$(CC) -O -c extra_writes.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH)
clean:
	rm -f *.o *.a *.gch $(addprefix ./debug/,*.o) 
	rm -f $(EXEC_NAMES) $(addprefix ./debug/, $(EXEC_NAMES))