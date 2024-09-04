#!/bin/sh

PRELOAD_ANCHOR_LIBS_PATH="/usr/lib/x86_64-linux-gnu/anchor_encr_scone/lib/"

export PRELOAD_LIBS=""

export SCONE_FLAGS=Hugepagesize=2097152 \
        LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/7/ \
        SCONE_VERSION=1 SCONE_LOG=0 SCONE_QUEUES=8 SCONE_NO_FS_SHIELD=1 \
        SCONE_NO_MMAP_ACCESS=1 SCONE_HEAP=8G SCONE_LD_DEBUG=1 SCONE_MODE=hw \
        /opt/scone/lib/ld-scone-x86_64.so.1

pushd /home/Anchor/src/benchmarks > /dev/null

for lib in $PRELOAD_ANCHOR_LIBS_LIST
do
        export PRELOAD_LIBS=$PRELOAD_ANCHOR_LIBS_PATH$lib:$PRELOAD_LIBS
done

export PRELOAD_LIBS=$PRELOAD_LIBS:$PRELOAD_GPERF_LIB_PATH$PRELOAD_GPERF_LIB

DATA_STRUCTURES="ctree btree rbtree hashmap_tx rtree"
REPEATS="1 2 3"

#echo $PRELOAD_LIBS
LD_PRELOAD=$PRELOAD_LIBS $SCONE_FLAGS ./pmembench KV_operations_ds.cfg | tee /results/anchor_encr_scone/KV_operations_ds_result
LD_PRELOAD=$PRELOAD_LIBS $SCONE_FLAGS ./pmembench KV_operations_th.cfg | tee /results/anchor_encr_scone/KV_operations_th_result
LD_PRELOAD=$PRELOAD_LIBS $SCONE_FLAGS ./pmembench anchor_pmembench_alloc_update_free.cfg | tee /results/anchor_encr_scone/anchor_pmembench_alloc_update_free_result
LD_PRELOAD=$PRELOAD_LIBS $SCONE_FLAGS ./pmembench anchor_pmembench_map.cfg | tee /results/anchor_encr_scone/anchor_pmembench_map_result
LD_PRELOAD=$PRELOAD_LIBS $SCONE_FLAGS ./pmembench anchor_pmembench_map_optimization.cfg | tee /results/anchor_encr_scone/anchor_pmembench_map_optimization

PRELOAD_ANCHOR_LIBS_PATH="/usr/lib/x86_64-linux-gnu/anchor_no_encr_scone/lib/"

export PRELOAD_LIBS=""

for lib in $PRELOAD_ANCHOR_LIBS_LIST
do
        export PRELOAD_LIBS=$PRELOAD_ANCHOR_LIBS_PATH$lib:$PRELOAD_LIBS
done

export PRELOAD_LIBS=$PRELOAD_LIBS:$PRELOAD_GPERF_LIB_PATH$PRELOAD_GPERF_LIB

LD_PRELOAD=$PRELOAD_LIBS $SCONE_FLAGS ./pmembench KV_operations_ds.cfg | tee /results/anchor_no_encr_scone/KV_operations_ds_result
LD_PRELOAD=$PRELOAD_LIBS $SCONE_FLAGS ./pmembench KV_operations_th.cfg | tee /results/anchor_no_encr_scone/KV_operations_th_result
LD_PRELOAD=$PRELOAD_LIBS $SCONE_FLAGS ./pmembench anchor_pmembench_alloc_update_free.cfg | tee /results/anchor_no_encr_scone/anchor_pmembench_alloc_update_free_result
LD_PRELOAD=$PRELOAD_LIBS $SCONE_FLAGS ./pmembench anchor_pmembench_map.cfg | tee /results/anchor_no_encr_scone/anchor_pmembench_map_result

popd > /dev/null