#!/bin/sh

# set up the libraries first
echo "Setting up the anchor libraries inside container"
echo "================================================"
./setup.sh -e scone -b benchmarks -a /usr/lib/x86_64-linux-gnu/
cp /usr/local/lib/libcrypto* /usr/lib/x86_64-linux-gnu/ 2>/dev/null
echo "================================================"

export SCONE_FLAGS="Hugepagesize=2097152 \
        LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/7/ \
        SCONE_VERSION=1 SCONE_LOG=0 SCONE_QUEUES=8 SCONE_NO_FS_SHIELD=1 \
        SCONE_NO_MMAP_ACCESS=1 SCONE_HEAP=2G SCONE_LD_DEBUG=1 SCONE_MODE=hw \
        /opt/scone/lib/ld-scone-x86_64.so.1"

cd /home/Anchor/src/benchmarks

DATA_STRUCTURES="ctree btree rbtree rtree hashmap_tx" #rtree
DATA_STRUCTURES_NON_OPT="ctree_non_opt btree_non_opt rbtree_non_opt"
REPEATS="1 2 3"

mkdir -p /results/anchor_encr_scone
mkdir -p /results/anchor_no_encr_scone

echo "Copying encrypted libraries to be found"
echo "================================================"
cp /usr/lib/x86_64-linux-gnu/anchor_encr_scone/lib/* /usr/lib/x86_64-linux-gnu/ 2>/dev/null

for i in $REPEATS
do
        # echo "================================================"
        # echo "Running encrypted KV_operations_ds"
        # echo "================================================"
        # eval "$SCONE_FLAGS ./pmembench KV_operations_ds.cfg > /results/anchor_encr_scone/KV_operations_ds_result"
        echo "Running encrypted KV_operations_th"
        echo "================================================"
        echo "Run "$i" $SCONE_FLAGS ./pmembench KV_operations_th.cfg > /results/anchor_encr_scone/KV_operations_th_result_"$i""
        eval "$SCONE_FLAGS ./pmembench KV_operations_th.cfg > /results/anchor_encr_scone/KV_operations_th_result_"$i""
        echo "Running encrypted anchor_pmembench_alloc_update_free"
        echo "================================================"
        echo "Run "$i" $SCONE_FLAGS ./pmembench anchor_pmembench_alloc_update_free.cfg > /results/anchor_encr_scone/anchor_pmembench_alloc_update_free_result_"$i""
        eval "$SCONE_FLAGS ./pmembench anchor_pmembench_alloc_update_free.cfg > /results/anchor_encr_scone/anchor_pmembench_alloc_update_free_result_"$i""
        echo "Running encrypted anchor_pmembench_map"
        echo "================================================"
        for ds in $DATA_STRUCTURES
        do
                echo "Run "$i" $SCONE_FLAGS ./pmembench anchor_pmembench_map_"$ds".cfg > /results/anchor_encr_scone/anchor_pmembench_map_"$ds"_result_"$i""
                eval "$SCONE_FLAGS ./pmembench anchor_pmembench_map_"$ds".cfg > /results/anchor_encr_scone/anchor_pmembench_map_"$ds"_result_"$i""
        done
        echo "Running encrypted rtree with 10k keys"
        echo "================================================"
        echo "Run "$i" $SCONE_FLAGS ./pmembench anchor_pmembench_map_rtree_small.cfg > /results/anchor_encr_scone/anchor_pmembench_map_rtree_small_result_"$i""
        eval "$SCONE_FLAGS ./pmembench anchor_pmembench_map_rtree_small.cfg > /results/anchor_encr_scone/anchor_pmembench_map_rtree_small_result_"$i""
        echo "Running encrypted anchor_pmembench_map_optimization (no-opt versions)"
        echo "================================================"
        for ds_no in $DATA_STRUCTURES_NON_OPT
        do
                echo "Run "$i" $SCONE_FLAGS ./pmembench anchor_pmembench_map_"$ds_no".cfg > /results/anchor_encr_scone/anchor_pmembench_map_"$ds_no"_result_"$i""
                eval "$SCONE_FLAGS ./pmembench anchor_pmembench_map_"$ds_no".cfg > /results/anchor_encr_scone/anchor_pmembench_map_"$ds_no"_result_"$i""
        done
done

echo "Copying non-encrypted libraries to be found"
echo "================================================"
cp /usr/lib/x86_64-linux-gnu/anchor_no_encr_scone/lib/* /usr/lib/x86_64-linux-gnu/ 2>/dev/null

for i in $REPEATS
do
        # echo "================================================"
        # echo "Running non-encrypted KV_operations_ds"
        # echo "================================================"
        # eval "$SCONE_FLAGS ./pmembench KV_operations_ds.cfg > /results/anchor_no_encr_scone/KV_operations_ds_result"
        echo "Running non-encrypted KV_operations_th"
        echo "================================================"
        echo "Run "$i" $SCONE_FLAGS ./pmembench KV_operations_th.cfg > /results/anchor_no_encr_scone/KV_operations_th_result_"$i""
        eval "$SCONE_FLAGS ./pmembench KV_operations_th.cfg > /results/anchor_no_encr_scone/KV_operations_th_result_"$i""
        echo "Running non-encrypted anchor_pmembench_alloc_update_free"
        echo "================================================"
        echo "Run "$i" $SCONE_FLAGS ./pmembench anchor_pmembench_alloc_update_free.cfg > /results/anchor_no_encr_scone/anchor_pmembench_alloc_update_free_result_"$i""
        eval "$SCONE_FLAGS ./pmembench anchor_pmembench_alloc_update_free.cfg > /results/anchor_no_encr_scone/anchor_pmembench_alloc_update_free_result_"$i""
        echo "Running non-encrypted anchor_pmembench_map"
        echo "================================================"
        for ds in $DATA_STRUCTURES
        do
                echo "Run "$i" $SCONE_FLAGS ./pmembench anchor_pmembench_map_"$ds".cfg > /results/anchor_no_encr_scone/anchor_pmembench_map_"$ds"_result_"$i""
                eval "$SCONE_FLAGS ./pmembench anchor_pmembench_map_"$ds".cfg > /results/anchor_no_encr_scone/anchor_pmembench_map_"$ds"_result_"$i""
        done
        echo "Running non-encrypted rtree with 10k keys"
        echo "================================================"
        echo "Run "$i" $SCONE_FLAGS ./pmembench anchor_pmembench_map_rtree_small.cfg > /results/anchor_no_encr_scone/anchor_pmembench_map_rtree_small_result_"$i""
        eval "$SCONE_FLAGS ./pmembench anchor_pmembench_map_rtree_small.cfg > /results/anchor_no_encr_scone/anchor_pmembench_map_rtree_small_result_"$i""
done

cd /home/Anchor/