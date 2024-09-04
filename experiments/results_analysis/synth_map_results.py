import subprocess
import os
import sys
import yaml
from parser import parse
from analyse import filter_out, analyse
from plot import plot
import pandas as pd
import numpy as np

import os, csv, argparse
from collections import defaultdict

pd.options.mode.chained_assignment = None  # default='warn'

title = "map_custom: map_custom [15] [group: pmemobj]"
title_non_opt = "map_custom: map_custom [18] [group: pmemobj]"

header = "total-avg[sec];ops-per-second[1/sec];total-max[sec];total-min[sec];total-median[sec];total-std-dev[sec];latency-avg[nsec];latency-min[nsec];latency-max[nsec];latency-std-dev[nsec];latency-pctl-50.0%[nsec];latency-pctl-99.0%[nsec];latency-pctl-99.9%[nsec];threads;ops-per-thread;data-size;seed;repeats;thread-affinity;main-affinity;min-exe-time;total-ops;type;seed;max-key;external-tx;alloc;keys;value-size;read-ratio;zipf-exp"

# important for the correct sequence in the final results csv
# 'ctree', 'btree', 'rtree', 'rbtree', 'hashmap_tx'
experiments = ["anchor_pmembench_map_ctree_result",
    "anchor_pmembench_map_btree_result",
    "anchor_pmembench_map_rtree_result",
    "anchor_pmembench_map_rbtree_result",
    "anchor_pmembench_map_hashmap_tx_result"]

experiments_non_opt = ["anchor_pmembench_map_ctree_non_opt_result",
    "anchor_pmembench_map_btree_non_opt_result",
    "anchor_pmembench_map_rbtree_non_opt_result"]
experiments_opt = ["anchor_pmembench_map_ctree_result",
    "anchor_pmembench_map_btree_result",
    "anchor_pmembench_map_rbtree_result"]

def synth_map_results(results_folder):
    results_path = results_folder
    for subdir, dirs, files in os.walk(results_path):
        for dir in dirs:
            if (dir.endswith("scone")):
                data = {}
                data_non_opt = {}
                for file in os.listdir(results_path+dir):
                    if (file.startswith("anchor_pmembench_map") and file.endswith("_result") and 
                        file != "anchor_pmembench_map_result" and file != "anchor_pmembench_map_non_opt_result"):   
                        if "non_opt" in file:
                            data_non_opt[file] = pd.read_csv(results_path+dir+'/'+file, delimiter=';', engine='python', skiprows=2, index_col=False, header=None)
                        else:
                            data[file] = pd.read_csv(results_path+dir+'/'+file, delimiter=';', engine='python', skiprows=2, index_col=False, header=None)
                
                open(results_path+dir+"/anchor_pmembench_map_result", "w+").write(title + "\n" + header + "\n")
                for exp in experiments:
                    open(results_path+dir+"/anchor_pmembench_map_result", "a").write(data[exp].to_csv(sep=';', index=False, index_label=False, header=None))

                if (len(data_non_opt) > 0):
                    open(results_path+dir+"/anchor_pmembench_map_non_opt_result", "w+").write(title_non_opt + "\n" + header + "\n" )
                    for exp in experiments_non_opt:
                        open(results_path+dir+"/anchor_pmembench_map_non_opt_result", "a").write(data[exp.replace("non_opt_","")].to_csv(sep=';', index=False, index_label=False, header=None))
                        open(results_path+dir+"/anchor_pmembench_map_non_opt_result", "a").write(data_non_opt[exp].to_csv(sep=';', index=False, index_label=False, header=None))

    return