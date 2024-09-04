CC=gcc
CFLAGS=-Wall -Werror -std=gnu99 -DANCHOR_FUNCS

ifeq ($(DEBUG),1)
CFLAGS += -Wp,-U_FORTIFY_SOURCE
CFLAGS += -ggdb -DDEBUG
else
CFLAGS += -O2 -U_FORTIFY_SOURCE -D_FORTIFY_SOURCE=2
endif

ifeq ($(ENCR_OFF),1)
CFLAGS += -DENCR_OFF
endif 

ifneq ($(SANITIZE),)
CFLAGS += -fsanitize=$(SANITIZE)
LDFLAGS += -fsanitize=$(SANITIZE)
#LDFLAGS += -fsanitize=$(tsanitize)
endif

INCS= -I. -I../common -I../include#to include the appropriate header files
FILE_NAMES=user_operations tx_user_operations manifest_operations 
FILE_NAMES+=hashmap openssl_gcm_encrypt trusted_counter metadata_operations metadata_log

ifeq ($(STATISTICS), 1)
CFLAGS += -DSTATISTICS
FILE_NAMES += internal_statistics
ifeq ($(WRITE_AMPL), 1)
CFLAGS += -DWRITE_AMPL
endif
endif

SRC_FILE_NAMES=$(addsuffix .c, $(FILE_NAMES))
HDR_FILE_NAMES=$(addsuffix .h, $(FILE_NAMES))
OBJ_FILE_NAMES=$(addsuffix .o, $(FILE_NAMES))
HEADER_FILES=../include/anchor/

ifeq ($(DEBUG),1)
LD_LIBRARY_PATH= -L../debug#to find libpmem and libpmemobj libraries
OBJ_DIR=../debug/anchor/#debug folder to place the .o files
LIB_BUILD_DIR=./build/lib/pmdk_debug/
LIB_DIR=../debug/#debug folder to place the lib file
LIB_SUBDIR=/pmdk_debug#debug folder for the install phase
else
LD_LIBRARY_PATH= -L../nondebug#to find libpmem and libpmemobj libraries
OBJ_DIR=../nondebug/anchor/#nondebug folder to place the .o files
LIB_BUILD_DIR=./build/lib/
LIB_DIR=../nondebug/#nondebug folder to place the lib file
LIB_SUBDIR=#nondebug folder for the install phase
endif

LIB_FILE=$(addprefix $(LIB_BUILD_DIR),libanchor.so)
LIB_FILE_STATIC=$(addprefix $(LIB_BUILD_DIR),libanchor.a)
OBJ_FILES=$(addprefix $(OBJ_DIR),$(OBJ_FILE_NAMES))
CLEAN_UP_OBJ = $(addprefix ../debug/anchor/,$(OBJ_FILE_NAMES)) $(addprefix ../nondebug/anchor/,$(OBJ_FILE_NAMES))
CLEAN_UP_OBJ += ../debug/anchor/internal_statistics.o ../nondebug/anchor/internal_statistics.o

LIBS_DESTDIR=$(libdir)#may need adaptations

all:$(OBJ_DIR) $(OBJ_FILES) $(HEADER_FILES) $(addprefix $(HEADER_FILES), $(wildcard *.h)) $(LIB_FILE) $(LIB_FILE_STATIC)
	+$(MAKE) -C microbenchmarks

$(OBJ_DIR):
	mkdir -p $(OBJ_DIR)

$(HEADER_FILES): 
	mkdir -p $(HEADER_FILES)

$(HEADER_FILES)%.h: %.h
	cp ./$< $@ 

#copy_headers: $(addsuffix .h, $(FILE_NAMES))
#	cp ./*.h $(HEADER_FILES)

$(OBJ_DIR)manifest_operations.o: manifest_operations.c manifest_operations.h
	$(CC) -fPIC -c manifest_operations.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH) -lpmem

$(OBJ_DIR)metadata_operations.o: metadata_operations.c metadata_operations.h
	$(CC) -fPIC -c metadata_operations.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH) -lpmem -lpmemobj

$(OBJ_DIR)metadata_log.o: metadata_log.c metadata_log.h
	$(CC) -fPIC -c metadata_log.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH) -lpmem
	
$(OBJ_DIR)user_operations.o: user_operations.c user_operations.h
	$(CC) -fPIC -c user_operations.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH) -lpmem -lpmemobj

$(OBJ_DIR)tx_user_operations.o: tx_user_operations.c tx_user_operations.h 
	$(CC) -fPIC -c tx_user_operations.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH) -lpmem -lpmemobj

$(OBJ_DIR)hashmap.o: hashmap.c hashmap.h 
	$(CC) -fPIC -c hashmap.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH) -lpthread

$(OBJ_DIR)openssl_gcm_encrypt.o: openssl_gcm_encrypt.c openssl_gcm_encrypt.h
	$(CC) -fPIC -c openssl_gcm_encrypt.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH) -lssl -lcrypto

$(OBJ_DIR)trusted_counter.o: trusted_counter.c trusted_counter.h
	$(CC) -fPIC -c trusted_counter.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH) -lpmem -lpthread

$(OBJ_DIR)internal_statistics.o: internal_statistics.c internal_statistics.h
	$(CC) -fPIC -c internal_statistics.c -o $@ $(CFLAGS) $(INCS) $(LD_LIBRARY_PATH) -lpthread

$(LIB_BUILD_DIR):
	mkdir -p $(LIB_BUILD_DIR)

$(LIB_FILE): $(LIB_BUILD_DIR) $(OBJ_FILES) $(SRC_FILE_NAMES) $(HDR_FILE_NAMES)
	$(CC) -shared -o $@ $(OBJ_FILES)
	cp $(LIB_FILE) $(addprefix $(LIB_DIR),libanchor.so)

$(LIB_FILE_STATIC): $(LIB_BUILD_DIR) $(OBJ_FILES) $(SRC_FILE_NAMES) $(HDR_FILE_NAMES)
	ar rcs $@ $(OBJ_FILES)
	cp $(LIB_FILE_STATIC) $(addprefix $(LIB_DIR),libanchor.a)

install:
	install -p -m 0755 $(addprefix $(LIB_BUILD_DIR),libanchor.so) $(LIBS_DESTDIR)$(LIB_SUBDIR)
	install -p -m 0755 $(addprefix $(LIB_BUILD_DIR),libanchor.a) $(LIBS_DESTDIR)$(LIB_SUBDIR)

uninstall:
	rm -rf $(addprefix $(LIB_DIR),libanchor.so) $(LIBS_DESTDIR)$(LIB_SUBDIR)
	rm -rf $(addprefix $(LIB_DIR),libanchor.a) $(LIBS_DESTDIR)$(LIB_SUBDIR)

clean:
	rm -f $(CLEAN_UP_OBJ)
	rm -f amcs arraypool arraypool_vanilla Manifest Manifest_new
	+$(MAKE) -C microbenchmarks clean
