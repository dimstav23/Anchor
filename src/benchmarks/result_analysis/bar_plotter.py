from parser import parse
from plotter import analyse_results, bar_plots, create_dir
import os
import sys

#paths & folders configuration
save_flag = 1 #flag to save the plots or only to show them
plot_save_dir = os.getcwd()+"/../plots/"

version_configs = [] #keeps the version of the code e.g. anchor_with_mmap...
for i in range(1, len(sys.argv)):
    version_configs.append(sys.argv[i])

result_folder_suffix = "results"
result_dirs = []
for config in version_configs:
    result_dirs.append(os.getcwd()+"/../" + config + '_' + result_folder_suffix)

#if we have to save the figures, check if the given directory exists or create it in any other case
if (save_flag):
    create_dir(plot_save_dir)

#results file parsing
benchmark_configs = []
accumulated_results = []
for i in range(0, len(result_dirs)):
    benchmark_configs.append([])
    for name in os.listdir(result_dirs[i]):
        benchmark_configs[i].append(name.split('_result')[0])
        #print(name.split('_result')[0])
    accumulated_results.append(parse(benchmark_configs[i], result_dirs[i]))
print(benchmark_configs)

#indices where the info, labels and values are stored in the results dictionary
info_idx = 0 #[category, number_of_experiments, group]
labels_idx = 1
values_idx = 2

#y axis metrics to consider
metrics = {
        #"exec_time"     :   "total-avg[sec]",    
        #"latency-avg"   :   "latency-avg[nsec]",
        "throughput"    :   "ops-per-second[1/sec]"
    }

#x axis possible variables
variants = {
        "threads"           :   "threads",    
        "ops-per-thread"    :   "ops-per-thread",
        "data-size"         :   "data-size",
        "seed"              :   "seed",
        "repeats"           :   "repeats",
        "thread-affinity"   :   "thread-affinity",
        "main-affinity"     :   "main-affinity",
        "min-exe-time"      :   "min-exe-time",
        "type-number"       :   "type-number",
        "operation"         :   "operation",
        "min-size"          :   "min-size",
        "lib"               :   "lib",
        "nestings"          :   "nestings",
        "min-rsize"         :   "min-rsize",
        "realloc-size"      :   "realloc-size",
        "changed-type"      :   "changed-type",
        "type"              :   "type"
    }

#config_to_plot = {
#        "data-size"     :   ["obj_tx_alloc_sizes_abort_nested", "obj_tx_alloc_sizes_atomic"],
#        "nestings"      :   ["obj_tx_free_nestings_abort_nested"]
#    }

#config_to_plot = {
#        "data-size"     :   ["obj_tx_alloc_thread_one_type_num"],
#        "realloc-size"  :   ["obj_tx_realloc_realloc_sizes_one_type_num"]
#    }

config_to_plot = {
        "data-size"     :   ["get_put_70_30_ds", "get_put_90_10_ds", "get_put_100_0_ds", "get_put_0_100_ds"],
        "threads"       :   ["get_put_70_30_th", "get_put_90_10_th", "get_put_100_0_th", "get_put_0_100_th"],
    }

results_tags = version_configs
x_values, y_values, info = analyse_results(accumulated_results, config_to_plot, variants, metrics, info_idx, labels_idx, values_idx)

bar_plots(x_values, y_values, config_to_plot, variants, metrics, results_tags, info, save_flag, plot_save_dir)