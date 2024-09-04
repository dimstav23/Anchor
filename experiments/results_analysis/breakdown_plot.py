import os
import numpy as np
import matplotlib.pyplot as plt
from breakdown_analyse import metrics,bench_info

import matplotlib
matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

info_idx = 0 #[category, number_of_experiments, group]
labels_idx = 1 #labels list
values_idx = 2 #values starting from idx 2

colour = ['darkgray','black','dimgray','lightgray', 'darkgray', 'gray', 'lightgrey']
#colour = ['grey', 'gold', 'red', 'palegreen', 'skyblue','blue','firebrick']
hatch = ['' , '' , '/' , '', '', '++', '/', '', '++']

#encr_colour = 'red'
encr_colour = ['firebrick', 'gold', 'red', 'palegreen', 'skyblue','blue','grey']
encr_hatch = ['' , '/' , '++' , '', '.', 'x']


def create_dir(new_dir):
    if not(os.path.exists(new_dir)):
        try:
            os.makedirs(new_dir)
        except OSError as error:
            print ("Creation of the directory %s failed : %s" % (new_dir, error))
        else:
            print ("Successfully created the directory %s" % new_dir)

def plot(plot_config, acc_results):
    plot_type = plot_config['plot']['plot_type']
    plot_folder = plot_config['plot']['plot_folder']
    x_axis_label = plot_config['plot']['x_axis']
    y_axis_label = plot_config['plot']['y_axis']
    variants = plot_config['plot']['variants']
    # print(variants)

    values = {}
    variant_values = {}
    x_axis_values = []

    for i in range (len(acc_results)):
        for benchmark in acc_results[i].keys():
            for variant in variants:
                variant_values[variant] = []
                variant_idx = acc_results[i][benchmark][labels_idx].index(variant)
                for j in range (len(acc_results[i][benchmark][values_idx])):
                    if (acc_results[i][benchmark][values_idx][j][variant_idx] not in variant_values[variant]):
                        variant_values[variant].append(acc_results[i][benchmark][values_idx][j][variant_idx])

            x_axis_label_idx = acc_results[i][benchmark][labels_idx].index(x_axis_label)
            for j in range (len(acc_results[i][benchmark][values_idx])):
                if (acc_results[i][benchmark][values_idx][j][x_axis_label_idx] not in x_axis_values):
                    x_axis_values.append(acc_results[i][benchmark][values_idx][j][x_axis_label_idx]) 
            
    #print(x_axis_values)
    #print(variant_values)
    idx=1
    plt.figure(figsize=(8,1.5))
    #number of plots = variants
    #number of bars per plot = x_axis * (lib-1)
    for benchmark in acc_results[i].keys():
        for variant_key in variant_values:
            for variant in variant_values[variant_key]:
                benchmarks = []
                read = []
                write = []
                alloc_free = []
                internal_log = []
                manifest_log = []
                cache_cleanup = []

                read_encr = []
                write_encr = []
                alloc_free_encr = []
                internal_log_encr = []
                manifest_log_encr = []
                tx = []
                misc = []
                ind = []
                for x_ax_val in x_axis_values:
                    for i in range (len(acc_results)): #0 for pmdk, 1 anchor_no_encr, 2 anchor_encr
                        variant_idx =  acc_results[i][benchmark][labels_idx].index(variant_key)
                        x_axis_label_idx = acc_results[i][benchmark][labels_idx].index(x_axis_label)
                        for j in range (len(acc_results[i][benchmark][values_idx])): # for each experiment
                            if i == 0: #pmdk - get reference
                                if (acc_results[i][benchmark][values_idx][j][x_axis_label_idx] == x_ax_val and acc_results[i][benchmark][values_idx][j][variant_idx] == variant):
                                    pmdk_execution_time = acc_results[i][benchmark][values_idx][j][0]
                                    pmdk_throughput = acc_results[i][benchmark][values_idx][j][1]
                                    #print(variant, x_ax_val, pmdk_execution_time, pmdk_throughput)

                            else:
                                if (acc_results[i][benchmark][values_idx][j][x_axis_label_idx] == x_ax_val and acc_results[i][benchmark][values_idx][j][variant_idx] == variant):
                                    read_idx =  acc_results[i][benchmark][labels_idx].index("READ_CYCLES") 
                                    write_idx =  acc_results[i][benchmark][labels_idx].index("WRITE_CYCLES")
                                    alloc_idx =  acc_results[i][benchmark][labels_idx].index("ALLOC_CYCLES") 
                                    zalloc_idx = acc_results[i][benchmark][labels_idx].index("ZALLOC_CYCLES") 
                                    free_idx =  acc_results[i][benchmark][labels_idx].index("FREE_CYCLES") 
                                    undo_idx =  acc_results[i][benchmark][labels_idx].index("UNDO_LOG_CYCLES") 
                                    redo_idx =  acc_results[i][benchmark][labels_idx].index("REDO_LOG_CYCLES") 
                                    manifest_idx =  acc_results[i][benchmark][labels_idx].index("MANIFEST_LOG_CYCLES") 
                                    tx_idx = acc_results[i][benchmark][labels_idx].index("TX_CYCLES") 
                                    encryption_idx =  acc_results[i][benchmark][labels_idx].index("ENCRYPTION_COST_CYCLES")
                                    cache_cleanup_idx = acc_results[i][benchmark][labels_idx].index("CACHE_CLEANUP_CYCLES")

                                    total_execution_time = acc_results[i][benchmark][values_idx][j][0]
                                    throughput = acc_results[i][benchmark][values_idx][j][1]
                                    overhead = float(pmdk_throughput) / float(throughput)
                                    time_overhead =  float(total_execution_time) / float(pmdk_execution_time) 
                                    # print(variant, x_ax_val, total_execution_time, throughput, overhead)

                                    values = acc_results

                                    if (i == 1):
                                        ind.append(x_ax_val + "\n w/o Enc")
                                        read.append(float(values[i][benchmark][values_idx][j][read_idx]) / float(pmdk_execution_time))
                                        write.append(float(values[i][benchmark][values_idx][j][write_idx]) / float(pmdk_execution_time))
                                        alloc_free.append((float(values[i][benchmark][values_idx][j][alloc_idx]) + float(values[i][benchmark][values_idx][j][zalloc_idx]) + float(values[i][benchmark][values_idx][j][free_idx])) / float(pmdk_execution_time))
                                        internal_log.append((float(values[i][benchmark][values_idx][j][redo_idx]) + float(values[i][benchmark][values_idx][j][undo_idx])) / float(pmdk_execution_time))
                                        manifest_log.append(float(values[i][benchmark][values_idx][j][manifest_idx]) / float(pmdk_execution_time))
                                        cache_cleanup.append(float(values[i][benchmark][values_idx][j][cache_cleanup_idx]) / float(pmdk_execution_time))
                                        read_encr.append(0)
                                        write_encr.append(0)
                                        alloc_free_encr.append(0)
                                        internal_log_encr.append(0)
                                        manifest_log_encr.append(0)
                                    elif (i == 2):
                                        ind.append(x_ax_val + "\n w/ Enc")
                                        read.append(read[-1])
                                        write.append(write[-1])
                                        alloc_free.append(alloc_free[-1])
                                        internal_log.append(internal_log[-1])
                                        manifest_log.append(manifest_log[-1])
                                        cache_cleanup.append(float(values[i][benchmark][values_idx][j][cache_cleanup_idx]) / float(pmdk_execution_time))
                                        read_encr.append(float(values[i][benchmark][values_idx][j][read_idx]) / float(pmdk_execution_time) - read[-1])
                                        write_encr.append(float(values[i][benchmark][values_idx][j][write_idx]) / float(pmdk_execution_time) - write[-1])
                                        alloc_free_encr.append((float(values[i][benchmark][values_idx][j][alloc_idx]) + float(values[i][benchmark][values_idx][j][zalloc_idx]) + float(values[i][benchmark][values_idx][j][free_idx])) / float(pmdk_execution_time) - alloc_free[-1])
                                        internal_log_encr.append((float(values[i][benchmark][values_idx][j][redo_idx]) + float(values[i][benchmark][values_idx][j][undo_idx])) / float(pmdk_execution_time) - internal_log[-1])
                                        manifest_log_encr.append(float(values[i][benchmark][values_idx][j][manifest_idx]) / float(pmdk_execution_time) - manifest_log[-1])

                                    remaining = overhead - read[-1] - write[-1] - alloc_free[-1] - internal_log[-1] - manifest_log[-1] -\
                                                read_encr[-1] - write_encr[-1] - alloc_free_encr[-1] - internal_log_encr[-1] - manifest_log_encr[-1]\
                                                - cache_cleanup[-1]
                                    
                                    misc.append(float(remaining))

                
                #print the plot here
                read = np.array(read)
                write = np.array(write)
                alloc_free = np.array(alloc_free)
                internal_log = np.array(internal_log)
                manifest_log = np.array(manifest_log)
                cache_cleanup = np.array(cache_cleanup)
                misc = np.array(misc)
                
                read_encr = np.array(read_encr)
                write_encr = np.array(write_encr)
                alloc_free_encr = np.array(alloc_free_encr)
                internal_log_encr = np.array(internal_log_encr)
                manifest_log_encr = np.array(manifest_log_encr)

                ## --------------------
                encryption_cost = read_encr + write_encr + alloc_free_encr + internal_log_encr + manifest_log_encr

                #fig2 = plt.figure()
                plt.subplot(1,2,idx)
                
                #fig = plt.figure()
                
                
                #ind = [x_ax_val for x_ax_val in x_axis_values]
                # print(ind)
                # print(variant_key + " = " + variant)
                width = 0.5

                # print("encryption cost " + str(encryption_cost))
                # print(misc)
                # print(encryption_cost)
                
                plt.bar(ind, encryption_cost, width=width, label='en-/decryption cost', color = encr_colour[0], hatch = encr_hatch[0], bottom=read  + write + alloc_free + internal_log + manifest_log + cache_cleanup + misc, alpha=.99)

                plt.bar(ind, misc, width=width, label='internal operations', color = colour[6], hatch = hatch[6], bottom=read + write + alloc_free + internal_log + + manifest_log + cache_cleanup, alpha=.99) 
                plt.bar(ind, cache_cleanup, width=width, label='cache cleanup', color = colour[5], hatch = hatch[5], bottom=read + write + alloc_free + internal_log + manifest_log, alpha=.99) 

                plt.bar(ind, manifest_log, width=width, label='manifest log', color = colour[4], hatch = hatch[4], bottom=read  + write + alloc_free + internal_log, alpha=.99)
                plt.bar(ind, internal_log, width=width, label='internal log', color = colour[3], hatch = hatch[3], bottom=read  + write + alloc_free, alpha=.99)               
                plt.bar(ind, alloc_free, width=width, label='alloc free', color = colour[2], hatch = hatch[2], bottom=read + write, alpha=.99)                
                plt.bar(ind, write, width=width,  label='write', color = colour[1], hatch = hatch[1], bottom=read, alpha=.99)                
                plt.bar(ind, read, width=width, label='read', color = colour[0], hatch = hatch[0], alpha=.99)
                    
                plt.xticks(ind, ind, fontsize=7)

                if (idx == 1):
                    plt.ylabel("Slowdown w.r.t. native PMDK", fontsize=8)
                #plt.xlabel("Persistent index", fontsize=8)
                if (idx == 2):
                    plt.legend(fontsize=7, loc='center left', bbox_to_anchor=(1, 0.5) )
                plt.title(variant + "% Get", fontsize=8)
                
                idx = idx+1
                #plot_file_path = plot_folder + " " + variant_key + " = " + variant
                #save_plot(plot_folder, plot_file_path, fig2)
    create_dir(plot_folder)
    plot_file_path = plot_folder + "/overhead_breakdown"  
    print(plot_file_path)
    plt.savefig(plot_file_path+".pdf", dpi=300, format='pdf', bbox_inches='tight')
    plt.savefig(plot_file_path+".png", dpi=300, format='png', bbox_inches='tight')