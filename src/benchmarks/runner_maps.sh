#!/bin/sh

export PKG_CONFIG_PATH=/usr/local/lib64/pkgconfig:/usr/local/lib/pkgconfig
export PMEM_IS_PMEM_FORCE=1
export PMEMOBJ_LOG_LEVEL=15
export PMEMOBJ_LOG_FILE=/home/sconeWorkspace/debug_folder/pmemobjLog
export PMEM_LOG_LEVEL=15
export PMEM_LOG_FILE=/home/sconeWorkspace/debug_folder/pmemLog
#for non-debug versions
export LD_LIBRARY_PATH=/usr/local/lib/:/home/sconeWorkspace/downloads/gperftools/build/lib/
export GPERF_PATH=/home/sconeWorkspace/downloads/gperftools/build/
#for debug versions
#export LD_LIBRARY_PATH=/usr/local/lib/pmdk_debug/:/usr/local/lib/

#scone
export SCONE_VERSION=1
export SCONE_LOG=7
export SCONE_NO_FS_SHIELD=1
export SCONE_NO_MMAP_ACCESS=1
export SCONE_HEAP=3584M
export SCONE_LD_DEBUG=1

PRELOAD_ANCHOR_LIBS_PATH="/usr/lib/x86_64-linux-gnu/anchor_encr_scone/lib/"
PRELOAD_ANCHOR_LIBS_LIST="libanchor.so libpmem.so.1 libpmemobj.so.1 libpmemlog.so.1 libpmemblk.so.1 libpmempool.so.1 librpmem.so.1"
PRELOAD_GPERF_LIB_PATH="/home/sconeWorkspace/downloads/gperftools/build/lib/"
PRELOAD_GPERF_LIB="libprofiler.so.0"

export PRELOAD_LIBS=""

for lib in $PRELOAD_ANCHOR_LIBS_LIST
do
        export PRELOAD_LIBS=$PRELOAD_ANCHOR_LIBS_PATH$lib:$PRELOAD_LIBS
done

export PRELOAD_LIBS=$PRELOAD_LIBS:$PRELOAD_GPERF_LIB_PATH$PRELOAD_GPERF_LIB

LD_PRELOAD=$PRELOAD_LIBS Hugepagesize=2097152 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/7/ SCONE_VERSION=1 SCONE_LOG=0 SCONE_QUEUES=8 SCONE_NO_FS_SHIELD=1 SCONE_NO_MMAP_ACCESS=1 SCONE_HEAP=8G SCONE_LD_DEBUG=1 SCONE_MODE=hw /opt/scone/lib/ld-scone-x86_64.so.1 ./pmembench anchor_pmembench_map.cfg | tee anchor_scone_encr_pmembench_map_result

PRELOAD_ANCHOR_LIBS_PATH="/usr/lib/x86_64-linux-gnu/anchor_no_encr_scone/lib/"
PRELOAD_ANCHOR_LIBS_LIST="libanchor.so libpmem.so.1 libpmemobj.so.1 libpmemlog.so.1 libpmemblk.so.1 libpmempool.so.1 librpmem.so.1"
PRELOAD_GPERF_LIB_PATH="/home/sconeWorkspace/downloads/gperftools/build/lib/"
PRELOAD_GPERF_LIB="libprofiler.so.0"

export PRELOAD_LIBS=""

for lib in $PRELOAD_ANCHOR_LIBS_LIST
do
        export PRELOAD_LIBS=$PRELOAD_ANCHOR_LIBS_PATH$lib:$PRELOAD_LIBS
done

export PRELOAD_LIBS=$PRELOAD_LIBS:$PRELOAD_GPERF_LIB_PATH$PRELOAD_GPERF_LIB

LD_PRELOAD=$PRELOAD_LIBS Hugepagesize=2097152 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/7/ SCONE_VERSION=1 SCONE_LOG=0 SCONE_QUEUES=8 SCONE_NO_FS_SHIELD=1 SCONE_NO_MMAP_ACCESS=1 SCONE_HEAP=8G SCONE_LD_DEBUG=1 SCONE_MODE=hw /opt/scone/lib/ld-scone-x86_64.so.1 ./pmembench anchor_pmembench_map.cfg | tee anchor_scone_no_encr_pmembench_map_result

#LD_PRELOAD=$PRELOAD_LIBS Hugepagesize=2097152 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/7/ SCONE_VERSION=1 SCONE_LOG=0 SCONE_QUEUES=8 SCONE_NO_FS_SHIELD=1 SCONE_NO_MMAP_ACCESS=1 SCONE_HEAP=8G SCONE_LD_DEBUG=1 SCONE_MODE=hw /opt/scone/lib/ld-scone-x86_64.so.1 ./pmembench anchor_pmembench_map.cfg
