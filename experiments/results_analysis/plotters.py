import os
import numpy as np
import matplotlib.pyplot as plt
from analyse import metrics,bench_info

import matplotlib
matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

versions_map = {
        "pmdk"                  :   "PMDK",    
        "anchor_encr"           :   "Native Anchor w/ Enc",
        "anchor_no_encr"        :   "Native Anchor w/o Enc",
        "anchor_encr_scone"     :   "Anchor",
        "anchor_no_encr_scone"  :   "Anchor w/o Enc"
    }

colour = ["white", "black", "white", "grey", "white", "white"]
hatch = ['--' , '' , '' , '', '/', '++']
markers = ['o', 's', '+', 'x', 'D', '*']

def create_dir(new_dir):
    if not(os.path.exists(new_dir)):
        try:
            os.makedirs(new_dir)
        except OSError as error:
            print ("Creation of the directory %s failed : %s" % (new_dir, error))
        else:
            print ("Successfully created the directory %s" % new_dir)

def save_plot(plot_dir, plot_file_path, fig, lgd):
    create_dir(plot_dir)
    #fig.set_size_inches(12, 5)
    print(plot_file_path)
    fig.savefig(plot_file_path + ".pdf", dpi=300, format='pdf', bbox_extra_artists=(lgd,), bbox_inches='tight')
    fig.savefig(plot_file_path + ".png", dpi=300, format='png', bbox_extra_artists=(lgd,), bbox_inches='tight')
    
def save_annot_plot(plot_dir, plot_file_path, fig, lgd, ann):
    create_dir(plot_dir)
    #fig.set_size_inches(12, 5)
    print(plot_file_path)
    fig.savefig(plot_file_path + ".pdf", dpi=300, format='pdf', bbox_extra_artists=(lgd, ann,), bbox_inches='tight')
    fig.savefig(plot_file_path + ".png", dpi=300, format='png', bbox_extra_artists=(lgd, ann,), bbox_inches='tight')

def ops_per_s(value):
    if value >= 1e6:
        value_str = f"{value / 1e6:.2f}\nMops"
    elif value >= 1e3:
        value_str = f"{value / 1e3:.2f}\nKops"
    else:
        value_str = f"{value:.2f}\nops"
    return value_str
    
def bar_plot(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar plot")
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            #number_of_bars = 3
            number_of_bars = len(x_values[benchmark][variant])
            bar_area_percentage = 0.8
            w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
            x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)
            
            fig = plt.figure()
            ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                internal_idx = list(x_values[benchmark][variant].keys()).index(version_lib)
                x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                rect = ax.bar(x_index, [float(i) for i in y_values[benchmark][variant][version_lib]], width = w, 
                                    color = colour[internal_idx], hatch = hatch[internal_idx], edgecolor = 'black', align='center', label=version_lib)
                #percentage change annotation
                if (y_axis_label=='throughput'):
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    else: 
                        if 'reference' in vars(): #if pmdk reference is defined
                            bar_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                            for j in range(len(rect)):
                                print(version_lib)
                                print(bar_values)
                                percentage_change = reference[j] / bar_values[j]
                                height = rect[j].get_height()
                                if (percentage_change != 1):
                                    ax.annotate('{:.2f}x'.format(percentage_change),
                                        xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                        xytext=(0, 3),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=4)
           
            #configure the look of the plot
            plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            for tick in ax.xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax.yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            ax.set_ylabel("Throughput ("+ metrics[y_axis_label] + ")", fontsize=10)
            ax.set_xlabel(bench_info[x_axis_label], fontsize=10)
            ax.set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            handles, labels = ax.get_legend_handles_labels()
            labels = [versions_map[label] for label in labels]
            lgd = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))          
            #save the plot
            plot_dir = plot_folder + "/" + benchmark
            plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
            save_plot(plot_dir, plot_file_path, fig, lgd)
            plt.close(fig) 
            #plt.show()
    return

def line_plot(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("line plot")
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            number_of_lines = len(x_values[benchmark][variant])

            fig = plt.figure()
            ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                internal_idx = list(x_values[benchmark][variant].keys()).index(version_lib)
                x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1)
                y_index = [float(i) for i in y_values[benchmark][variant][version_lib]]
                line = ax.plot(x_index, y_index, color = 'black', marker=markers[internal_idx], linestyle='dashed', markersize=6, label=version_lib)

                #percentage change annotation
                if (y_axis_label=='throughput'):
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    else:
                        if 'reference' in vars(): #if pmdk reference is defined
                            line_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                            for j in range(len(line_values)):
                                percentage_change = reference[j] / line_values[j]
                                if (percentage_change != 1):
                                    ax.annotate('{:.2f}x'.format(percentage_change),
                                        xy=(j, line_values[j]),
                                        xytext=(0, 5),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=6)
                            
            #configure the look of the plot
            plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            #ax.xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
            #ax.xaxis.set_ticklabels(x_values[benchmark][variant][version_lib])
            for tick in ax.xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax.yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            ax.set_ylabel(metrics[y_axis_label], fontsize=10)
            ax.set_xlabel(bench_info[x_axis_label], fontsize=10)
            ax.set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            handles, labels = ax.get_legend_handles_labels()
            labels = [versions_map[label] for label in labels]
            lgd = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))      
            
            #save the plot
            plot_dir = plot_folder + "/" + benchmark
            plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
            save_plot(plot_dir, plot_file_path, fig, lgd)
            plt.close(fig)

    return

def bar_overhead_plot(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar overhead plot")
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            #number_of_bars = 3
            number_of_bars = len(x_values[benchmark][variant]) - 1 #minus the first one which is the reference
            bar_area_percentage = number_of_bars * 0.2 #0.8
            w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
            x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)
            fig = plt.figure()
            ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                if (version_lib == "pmdk"):
                    reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    #print(reference)
                else:
                    internal_idx = (list(x_values[benchmark][variant].keys()).index(version_lib))-1
                    x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                    lib_values = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    values_to_plot = [x/y for x, y in zip(reference, lib_values)]
                    #print(x_index)
                    rect = ax.bar(x_index, values_to_plot, width = w, 
                                        color = colour[internal_idx], hatch = hatch[internal_idx], 
                                        edgecolor = 'black', align='center', label=version_lib)
                
                #percentage change annotation
                if (y_axis_label=='throughput'):
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    else: 
                        if 'reference' in vars(): #if pmdk reference is defined
                            bar_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                            for j in range(len(rect)):
                                percentage_change = reference[j] / bar_values[j]
                                height = rect[j].get_height()
                                if (percentage_change != 1):
                                    ax.annotate('{:.2f}x'.format(percentage_change),
                                        xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                        xytext=(0, 3),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=3)

            #configure the look of the plot
            plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
            for tick in ax.xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax.yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            #ax.set_ylabel(metrics[y_axis_label], fontsize=10)
            ax.set_ylabel("Relative throughput overhead w.r.t. pmdk native", fontsize=10)
            ax.set_xlabel(bench_info[x_axis_label], fontsize=10)
            ax.set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            handles, labels = ax.get_legend_handles_labels()
            labels = [versions_map[label] for label in labels]
            lgd = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))
            
            #save the plot
            plot_dir = plot_folder + "/" + benchmark
            plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
            save_plot(plot_dir, plot_file_path, fig, lgd)
            plt.close(fig)
            
    return

#BAR PLOT FOR TRANSACTION OPERATIONS
def bar_plot_tx_op(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar plot tx op")
    fig, ax = plt.subplots(1, 3)
    ax_idx = 0
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            #number_of_bars = 3
            number_of_bars = len(x_values[benchmark][variant])
            bar_area_percentage = 0.8
            w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
            x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)
            
            #fig = plt.figure()
            #ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                internal_idx = list(x_values[benchmark][variant].keys()).index(version_lib)
                x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values[benchmark][variant][version_lib]], width = w, 
                                    color = colour[internal_idx], hatch = hatch[internal_idx],
                                    edgecolor = 'black', align='center', label=version_lib)
                #percentage change annotation
                '''
                if (y_axis_label=='throughput'):
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    else: 
                        if 'reference' in vars(): #if pmdk reference is defined
                            bar_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                            for j in range(len(rect)):
                                percentage_change = reference[j] / bar_values[j]
                                height = rect[j].get_height()
                                if (percentage_change != 1):
                                    ax[ax_idx].annotate('{:.2f}x'.format(percentage_change),
                                        xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                        xytext=(0, 3),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=3)
                '''
            #configure the look of the plot
            #plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            #print(x_values[benchmark][variant][version_lib])
            custom_x_ticks = list(map(float,x_values[benchmark][variant][version_lib]))
            custom_x_ticks = [round(a) for a in custom_x_ticks]

            ax[ax_idx].xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
            ax[ax_idx].xaxis.set_ticklabels(custom_x_ticks)
            #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
            for tick in ax[ax_idx].xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax[ax_idx].yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            
            if (ax_idx == 0):
                if (metrics[y_axis_label] == "ops-per-second[1/sec]"):
                    ax[ax_idx].set_ylabel("Ops/sec", fontsize=10)
                else:
                    ax[ax_idx].set_ylabel(metrics[y_axis_label], fontsize=10)

            
            if (bench_info[x_axis_label] == "data-size"):
                if (ax_idx == 1):
                    ax[ax_idx].set_xlabel("object size (B)", fontsize=10)
            else:
                ax[ax_idx].set_xlabel(bench_info[x_axis_label], fontsize=10)
            
            if plot_title == "put":
                plot_title = "alloc"
            elif plot_title == "delete":
                plot_title = "free"
            #ax[ax_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            ax[ax_idx].set_title(plot_title, fontsize=10)
            handles, labels = ax[ax_idx].get_legend_handles_labels()
            labels = [versions_map[label] for label in labels]
            if (ax_idx == 2): #to set the legend once
                '''
                lgd = ax[ax_idx].legend(handles, labels, loc='upper center', #bbox_to_anchor=(0.5,-0.12),
                                        bbox_to_anchor=(0., 1.22, 1., .102), #loc='lower left',
                                        ncol=len(labels) , borderaxespad=0.)  
                '''
                lgd = ax[ax_idx].legend(handles, labels,  #bbox_to_anchor=(0.5,-0.12),
                                        bbox_to_anchor=(1.03, 1), loc='upper left', borderaxespad=0.,
                                        ncol=1, fontsize=10)      
            ax_idx = ax_idx + 1

    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
    fig.set_size_inches(12, 1.85)
    save_plot(plot_dir, plot_file_path, fig, lgd)
    plt.close(fig) 
    #plt.show()
    return

#BAR PLOT FOR TRANSACTION OPERATIONS
def bar_plot_tx_op_overhead(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):

    print("bar plot tx op_overhead")
    fig, ax = plt.subplots(1, 3)
    ax_idx = 0
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            print("Experiment parameters: " + str(benchmark))
            #number_of_bars = 3
            number_of_bars = len(x_values[benchmark][variant]) - 1
            bar_area_percentage = 0.8
            w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
            x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:   
                if (version_lib == "pmdk"):
                    reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    #print(reference)
                else:
                    internal_idx = (list(x_values[benchmark][variant].keys()).index(version_lib))-1
                    x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                    lib_values = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    values_to_plot = [x/y for x, y in zip(reference, lib_values)]
                    #print(x_index)
                    formatted_values = ["%.2f" % value for value in values_to_plot]
                    print(str(version_lib) + " " + str(x_values[benchmark][variant][version_lib]) + " overheads : " + str(formatted_values))
                    rect = ax[ax_idx].bar(x_index, values_to_plot, width = w, 
                                        color = colour[internal_idx], hatch = hatch[internal_idx], 
                                        edgecolor = 'black', align='center', label=version_lib)
                                          
                #percentage change annotation
                '''
                if (y_axis_label=='throughput'):
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    else: 
                        if 'reference' in vars(): #if pmdk reference is defined
                            bar_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                            for j in range(len(rect)):
                                percentage_change = reference[j] / bar_values[j]
                                height = rect[j].get_height()
                                if (percentage_change != 1):
                                    ax[ax_idx].annotate('{:.2f}x'.format(percentage_change),
                                        xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                        xytext=(0, 3),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=3)
               ''' 
            #configure the look of the plot
            #plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            #print(x_values[benchmark][variant][version_lib])

            custom_x_ticks = list(map(float,x_values[benchmark][variant][version_lib]))
            custom_x_ticks = [round(a) for a in custom_x_ticks]

            ax[ax_idx].xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
            ax[ax_idx].xaxis.set_ticklabels(custom_x_ticks)
            #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
            for tick in ax[ax_idx].xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax[ax_idx].yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            
            if (ax_idx == 0):
                if (metrics[y_axis_label] == "ops-per-second[1/sec]"):
                    ax[ax_idx].set_ylabel("Ops/sec", fontsize=10)
                else:
                    ax[ax_idx].set_ylabel(metrics[y_axis_label], fontsize=10)

            
            if (bench_info[x_axis_label] == "data-size"):
                if (ax_idx == 1):
                    ax[ax_idx].set_xlabel("object size (B)", fontsize=10)
            else:
                ax[ax_idx].set_xlabel(bench_info[x_axis_label], fontsize=10)
            
            if plot_title == "put":
                plot_title = "alloc"
            elif plot_title == "delete":
                plot_title = "free"
            #ax[ax_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            ax[ax_idx].set_title(plot_title, fontsize=10)
            handles, labels = ax[ax_idx].get_legend_handles_labels()
            labels = [versions_map[label] for label in labels]
            if (ax_idx == 1): #to set the legend once
                lgd = ax[ax_idx].legend(handles, labels, loc='upper center', #bbox_to_anchor=(0.5,-0.12),
                                        bbox_to_anchor=(0., 1.22, 1., .102), #loc='lower left',
                                        ncol=len(labels) , borderaxespad=0.)          
            ax_idx = ax_idx + 1

    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_overhead_" + '_'.join(map(str, experiment_params))
    fig.set_size_inches(12, 2)
    save_plot(plot_dir, plot_file_path, fig, lgd)
    plt.close(fig) 
    #plt.show()
    return

#BAR PLOT FOR DATA SIZE RESULTS
def bar_plot_ds(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar plot ds")
    fig, ax = plt.subplots(1, 3)
    ax_idx = 0
    for benchmark in x_values:
        #plot_title = benchmark
        if (benchmark.split("_")[2] == "0"):
            plot_title = benchmark.split("_")[3] + "% W"
        else:
            plot_title = benchmark.split("_")[2] + "% R"
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            #number_of_bars = 3
            number_of_bars = len(x_values[benchmark][variant])
            bar_area_percentage = 0.8
            w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
            x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)
    
            #fig = plt.figure()
            #ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                internal_idx = list(x_values[benchmark][variant].keys()).index(version_lib)
                x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values[benchmark][variant][version_lib]], width = w, 
                                    color = colour[internal_idx], hatch = hatch[internal_idx],
                                    edgecolor = 'black', align='center', label=version_lib)
                #percentage change annotation
                '''
                if (y_axis_label=='throughput'):
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    else: 
                        if 'reference' in vars(): #if pmdk reference is defined
                            bar_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                            for j in range(len(rect)):
                                percentage_change = reference[j] / bar_values[j]
                                height = rect[j].get_height()
                                if (percentage_change != 1):
                                    ax[ax_idx].annotate('{:.2f}x'.format(percentage_change),
                                        xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                        xytext=(0, 3),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=5)
                '''
            #configure the look of the plot
            #plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            ax[ax_idx].xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
            ax[ax_idx].xaxis.set_ticklabels(x_values[benchmark][variant][version_lib])
            #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
            for tick in ax[ax_idx].xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax[ax_idx].yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            if (ax_idx == 0):
                if (metrics[y_axis_label] == "ops-per-second[1/sec]"):
                    ax[ax_idx].set_ylabel("Ops/sec", fontsize=10)
                else:
                    ax[ax_idx].set_ylabel(metrics[y_axis_label], fontsize=10)
            
            if (bench_info[x_axis_label] == "data-size"):
                if ax_idx == 1:
                    ax[ax_idx].set_xlabel("object size (bytes)", fontsize=10)
            else:
                ax[ax_idx].set_xlabel(bench_info[x_axis_label], fontsize=10)
            #ax[ax_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            ax[ax_idx].set_title(plot_title, fontsize=10)
            handles, labels = ax[ax_idx].get_legend_handles_labels()
            labels = [versions_map[label] for label in labels]
            if (ax_idx == 1): #to set the legend once and in the middle
                lgd = ax[ax_idx].legend(handles, labels, loc='upper center', #bbox_to_anchor=(0.5,-0.12),
                                        bbox_to_anchor=(0., 1.22, 1., .102), #loc='lower left',
                                        ncol=len(labels), borderaxespad=0.)           
            ax_idx += 1

    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
    fig.set_size_inches(12, 2)
    save_plot(plot_dir, plot_file_path, fig, lgd)
    plt.close(fig) 
    #plt.show()
    return

#LINE PLOT FOR THREAD RESULTS
def line_plot_th(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("line plot th")
    fig, ax = plt.subplots(1, 3)
    ax_idx = 0
    for benchmark in x_values:
        if (benchmark.split("_")[2] == "0"):
            plot_title = benchmark.split("_")[3] + "% W"
        else:
            plot_title = benchmark.split("_")[2] + "% R"
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            number_of_lines = len(x_values[benchmark][variant])

            #fig = plt.figure()
            #ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                internal_idx = list(x_values[benchmark][variant].keys()).index(version_lib)
                x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1)
                y_index = [float(i) * 100 for i in y_values[benchmark][variant][version_lib]]

                print(experiment_params)
                formatted_values = ["%.2f" % float(value) for value in y_values[benchmark][variant][version_lib]]
                print(str(version_lib) + " " + str(x_values[benchmark][variant][version_lib]) + " throughput : " + str(formatted_values))

                line = ax[ax_idx].plot(x_index, y_index, marker=markers[internal_idx], linestyle='dashed', markersize=6, label=version_lib,
                                        color = 'black')

                #percentage change annotation
                '''
                if (y_axis_label=='throughput'):
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    else:
                        if 'reference' in vars(): #if pmdk reference is defined
                            line_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                            for j in range(len(line_values)):
                                percentage_change = reference[j] / line_values[j]
                                if (percentage_change != 1):
                                    ax[ax_idx].annotate('{:.2f}x'.format(percentage_change),
                                        xy=(j, line_values[j]),
                                        xytext=(0, 5),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=5)
                '''         
            #configure the look of the plot
            #plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])

            custom_x_ticks = list(map(float,x_values[benchmark][variant][version_lib]))
            custom_x_ticks = [round(a) for a in custom_x_ticks]

            ax[ax_idx].xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
            ax[ax_idx].xaxis.set_ticklabels(custom_x_ticks)
            for tick in ax[ax_idx].xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax[ax_idx].yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            
            if (ax_idx == 0):
                if (metrics[y_axis_label] == "ops-per-second[1/sec]"):
                    ax[ax_idx].set_ylabel("Ops/sec", fontsize=10)
                else:
                    ax[ax_idx].set_ylabel(metrics[y_axis_label], fontsize=10)

            
            if (bench_info[x_axis_label]== "threads"):
                if ax_idx == 1:
                    ax[ax_idx].set_xlabel("Number of threads", fontsize=10)
            else:
                ax[ax_idx].set_xlabel(bench_info[x_axis_label], fontsize=10)
            #ax[ax_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            ax[ax_idx].set_title(plot_title, fontsize=10)
            handles, labels = ax[ax_idx].get_legend_handles_labels()
            labels = [versions_map[label] for label in labels]
            

            if (ax_idx == 1): #to set the legend once in the middle
                lgd = ax[ax_idx].legend(handles, labels, loc='upper center', #bbox_to_anchor=(0.5,-0.12),
                                        bbox_to_anchor=(0., 1.22, 1., .102), #loc='lower left',
                                        ncol=len(labels) , borderaxespad=0.)        
            
            ax_idx += 1

    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
    fig.set_size_inches(12, 2)
    save_plot(plot_dir, plot_file_path, fig, lgd)
    plt.close(fig) 
    #plt.show()
    return

#OVERHEAD BAR PLOT FOR INSERT/UPDATE/REMOVE 500K-1M
def bar_overhead_plot_common(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar overhead plot")
    fig1, ax1 = plt.subplots(1, 3)
    fig2, ax2 = plt.subplots(1, 3)
    ax_idx1 = 0
    ax_idx2 = 0
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            if (variant == '+total-ops=500000'):
                experiment_params = variant.split('+')[1:]
                #number_of_bars = 3
                number_of_bars = len(x_values[benchmark][variant]) - 1 #minus the first one which is the reference
                bar_area_percentage = number_of_bars * 0.2 #0.8
                w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
                x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)
                #fig = plt.figure()
                #ax = fig.add_subplot(111)

                #append values to the plot
                for version_lib in x_values[benchmark][variant]:     
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                        #print(reference)
                    else:
                        internal_idx = (list(x_values[benchmark][variant].keys()).index(version_lib))-1
                        x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                        lib_values = [float(i) for i in y_values[benchmark][variant][version_lib]]
                        values_to_plot = [x/y for x, y in zip(reference, lib_values)]
                        #print(x_index)
                        rect = ax1[ax_idx1].bar(x_index, values_to_plot, width = w, 
                                            color = colour[internal_idx], hatch = hatch[internal_idx],
                                            edgecolor = 'black', align='center', label=version_lib)
                    
                    #percentage change annotation
                    if (y_axis_label=='throughput'):
                        if (version_lib == "pmdk"):
                            reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                        else: 
                            if 'reference' in vars(): #if pmdk reference is defined
                                bar_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                                for j in range(len(rect)):
                                    percentage_change = reference[j] / bar_values[j]
                                    height = rect[j].get_height()
                                    if (percentage_change != 1):
                                        ax1[ax_idx1].annotate('{:.2f}x'.format(percentage_change),
                                            xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                            xytext=(0, 3),  # 3 points vertical offset
                                            textcoords="offset points",
                                            ha='center', va='bottom', size=8)
                #configure the look of the plot
                #plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
                print(x_values[benchmark][variant][version_lib])
                ax1[ax_idx1].xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
                ax1[ax_idx1].xaxis.set_ticklabels(x_values[benchmark][variant][version_lib])
                #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
                for tick in ax1[ax_idx1].xaxis.get_major_ticks():
                    tick.label.set_fontsize(8)
                for tick in ax1[ax_idx1].yaxis.get_major_ticks():
                    tick.label.set_fontsize(8)
                #ax.set_ylabel(metrics[y_axis_label], fontsize=10)
                ax1[ax_idx1].set_ylabel("Relative throughput overhead w.r.t. pmdk native", fontsize=10)
                ax1[ax_idx1].set_xlabel(bench_info[x_axis_label], fontsize=10)
                ax1[ax_idx1].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
                handles, labels = ax1[ax_idx1].get_legend_handles_labels()
                if (ax_idx1 == 1):
                    lgd1 = ax1[ax_idx1].legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))  
                #save the plot
                #plot_dir = plot_folder + "/" + benchmark
                #plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
                #save_plot(plot_dir, plot_file_path, fig, lgd)
                #plt.close(fig)
                ax_idx1 = ax_idx1 + 1
            else:
                experiment_params = variant.split('+')[1:]
                #number_of_bars = 3
                number_of_bars = len(x_values[benchmark][variant]) - 1 #minus the first one which is the reference
                bar_area_percentage = number_of_bars * 0.2 #0.8
                w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
                x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)

                #fig = plt.figure()
                #ax = fig.add_subplot(111)

                #append values to the plot
                for version_lib in x_values[benchmark][variant]:     
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                        #print(reference)
                    else:
                        internal_idx = (list(x_values[benchmark][variant].keys()).index(version_lib))-1
                        x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                        lib_values = [float(i) for i in y_values[benchmark][variant][version_lib]]
                        values_to_plot = [x/y for x, y in zip(reference, lib_values)]
                        #print(x_index)
                        rect = ax2[ax_idx2].bar(x_index, values_to_plot, width = w, 
                                            color = colour[internal_idx], hatch = hatch[internal_idx],
                                            edgecolor = 'black', align='center', label=version_lib)
                    
                    #percentage change annotation
                    if (y_axis_label=='throughput'):
                        if (version_lib == "pmdk"):
                            reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                        else: 
                            if 'reference' in vars(): #if pmdk reference is defined
                                bar_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                                for j in range(len(rect)):
                                    percentage_change = reference[j] / bar_values[j]
                                    height = rect[j].get_height()
                                    if (percentage_change != 1):
                                        ax2[ax_idx2].annotate('{:.2f}x'.format(percentage_change),
                                            xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                            xytext=(0, 3),  # 3 points vertical offset
                                            textcoords="offset points",
                                            ha='center', va='bottom', size=8)
                #configure the look of the plot
                #plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
                print(x_values[benchmark][variant][version_lib])
                ax2[ax_idx2].xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
                ax2[ax_idx2].xaxis.set_ticklabels(x_values[benchmark][variant][version_lib])
                #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
                for tick in ax2[ax_idx2].xaxis.get_major_ticks():
                    tick.label.set_fontsize(8)
                for tick in ax2[ax_idx2].yaxis.get_major_ticks():
                    tick.label.set_fontsize(8)
                #ax.set_ylabel(metrics[y_axis_label], fontsize=10)
                ax2[ax_idx2].set_ylabel("Relative throughput overhead w.r.t. pmdk native", fontsize=10)
                ax2[ax_idx2].set_xlabel(bench_info[x_axis_label], fontsize=10)
                ax2[ax_idx2].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
                handles, labels = ax2[ax_idx2].get_legend_handles_labels()
                if (ax_idx2 == 1):
                    lgd2 = ax2[ax_idx2].legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))  
                #save the plot
                #plot_dir = plot_folder + "/" + benchmark
                #plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
                #save_plot(plot_dir, plot_file_path, fig, lgd)
                #plt.close(fig)
                ax_idx2 = ax_idx2 + 1
    
    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))+ "total-ops=500000"
    save_plot(plot_dir, plot_file_path, fig1, lgd1)
    plt.close(fig1)   
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))+ "total-ops=1000000"
    save_plot(plot_dir, plot_file_path, fig2, lgd2)
    plt.close(fig2)            
    return

#BAR OVERHEAD PLOT FOR MAP CUSTOM WORKLOADS
def bar_overhead_plot_custom(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar_overhead_plot_custom")
    fig1, ax1 = plt.subplots(1, 3)
    ax_idx1 = 0
    fig2, ax2 = plt.subplots(1, 3)
    ax_idx2 = 0
    
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            print("Experiment parameters: " + str(experiment_params))
            if (experiment_params[0] == 'total-ops=10000000' and experiment_params[2] == 'keys=100000'):
                ax = ax1[ax_idx1]
            elif (experiment_params[0] == 'total-ops=10000000' and experiment_params[2] == 'keys=500000'):
                ax = ax2[ax_idx2] 
            else:
                fig = plt.figure()
                ax = fig.add_subplot(111)
            
            #number_of_bars = 3
            number_of_bars = len(x_values[benchmark][variant]) - 1 #minus the first one which is the reference
            bar_area_percentage = number_of_bars * 0.2 #0.8
            w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
            x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)
            
            #fig = plt.figure()
            #ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                if (version_lib == "pmdk"):
                    reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    # print("reference " + str(reference))
                else:
                    internal_idx = (list(x_values[benchmark][variant].keys()).index(version_lib))-1
                    x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                    lib_values = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    values_to_plot = [x/y for x, y in zip(reference, lib_values)]
                    formatted_values = ["%.2f" % value for value in values_to_plot]
                    print(str(version_lib) + " " + str(x_values[benchmark][variant][version_lib]) + " overheads : " + str(formatted_values))
                    rect = ax.bar(x_index, values_to_plot, width = w, 
                                        color = colour[internal_idx], hatch = hatch[internal_idx],
                                        edgecolor = 'black', align='center', label=version_lib)
                
                #percentage change annotation
                '''
                if (y_axis_label=='throughput'):
                    if (version_lib == "pmdk"):
                        reference = [float(i) for i in y_values[benchmark][variant][version_lib]]
                    else: 
                        if 'reference' in vars(): #if pmdk reference is defined
                            bar_values = [float(item) for item in y_values[benchmark][variant][version_lib]]
                            for j in range(len(rect)):
                                percentage_change = reference[j] / bar_values[j]
                                height = rect[j].get_height()
                                if (percentage_change != 1):
                                    ax.annotate('{:.2f}x'.format(percentage_change),
                                        xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                        xytext=(0, 3),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=5)
                '''
                if (y_axis_label=='throughput'):
                    if (version_lib == "pmdk"):
                        baseline = [float(i) for i in y_values[benchmark][variant][version_lib]]
            # print(baseline)
            #configure the look of the plot
            plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            ax.xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))

            x_labels = x_values[benchmark][variant][version_lib]
            for i in range (len(x_labels)):
                if x_labels[i]=="hashmap_tx":
                    x_labels[i]="hashmap"
                
            ax.xaxis.set_ticklabels(x_labels)
            #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
            for tick in ax.xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax.yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            #ax.set_ylabel(metrics[y_axis_label], fontsize=10)
            #if (ax_idx1 == 0):
            #    ax.set_ylabel("Relative throughput overhead w.r.t. pmdk native", fontsize=10)
            
            #ax.set_xlabel(bench_info[x_axis_label], fontsize=10)
            
            # Annotate the baseline numbers
            for i, label in enumerate(ax.get_xticklabels()):
                ax.annotate(f'{ops_per_s(baseline[i])}', xy=(label.get_position()[0], 0), xytext=(0, -19), 
                            textcoords='offset points', ha='center', va='top', arrowprops=None,
                            fontsize=8)
                
            #ax.set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            #print(experiment_params)
            ax.set_title(experiment_params[1].split("=")[1] + "% Get", fontsize=10)
            #ax.set_title(' '.join(map(str, experiment_params)), fontsize=10)
            handles, labels = ax.get_legend_handles_labels()
            labels = [versions_map[label] for label in labels]
            #save the plot
            #plot_dir = plot_folder + "/" + benchmark
            #plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
            #save_plot(plot_dir, plot_file_path, fig, lgd)
            #plt.close(fig)
            #print(experiment_params)
            if (experiment_params[0] == 'total-ops=10000000' and experiment_params[2] == 'keys=100000'):
                if (ax_idx1 == 0):
                    ax.set_ylabel("Slowdown w.r.t. native PMDK", fontsize=10)
                if (ax_idx1 == 1):
                    #lgd1 = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))
                    lgd1 = ax.legend(handles, labels, loc='upper center', #bbox_to_anchor=(0.5,-0.12),
                                        bbox_to_anchor=(0., 1.22, 1., .102), #loc='lower left',
                                        ncol=len(labels) , borderaxespad=0.)  
                ax1[ax_idx1] = ax
                ax_idx1 = ax_idx1 + 1
            elif (experiment_params[0] == 'total-ops=10000000' and experiment_params[2] == 'keys=500000'):
                if (ax_idx2 == 0):
                    ax.set_ylabel("Slowdown w.r.t. PMDK native", fontsize=10)
                if (ax_idx2 == 1):
                    #lgd2 = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))
                    lgd2 = ax.legend(handles, labels, loc='upper center', #bbox_to_anchor=(0.5,-0.12),
                                        bbox_to_anchor=(0., 1.22, 1., .102), #loc='lower left',
                                        ncol=len(labels) , borderaxespad=0.)    
                ax2[ax_idx2] = ax
                ax_idx2 = ax_idx2 + 1
            else:
                lgd = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))
                plt.close(fig)
    
    
    # for i, ax in enumerate(ax1):
    #     for j, label in enumerate(ax.get_xticklabels()):
    #         # print(label.get_position()[0])
    #         ax.annotate(f'{j+1}', xy=(label.get_position()[0], 0), xytext=(0, -15), 
    #                     textcoords='offset points', ha='center', va='top', arrowprops=None,
    #                     fontsize=7)
    # fig1.subplots_adjust(bottom=0.15)
    ann = ax.annotate(f'Baseline:', xy=(0, 0), xytext=(-510, -23), 
                            textcoords='offset points', ha='center', va='top', arrowprops=None,
                            fontsize=8)
       
    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params)) + "100k"
    fig1.set_size_inches(12, 2)
    save_annot_plot(plot_dir, plot_file_path, fig1, lgd1, ann)     
    #plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params)) + "500k"
    #fig2.set_size_inches(12, 2)
    #save_plot(plot_dir, plot_file_path, fig2, lgd2)     
    return

def bar_plot_opt(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar plot opt")

    #lib is hardcoded
    lib = 'anchor_encr_scone'
    
    for benchmark in x_values:
        plot_title = benchmark
        fig = plt.figure(figsize=(4.5,2))
        #fig = plt.figure()
        ax = fig.add_subplot(111)
        space_idx = 0
        labels=[]
        for variant in x_values[benchmark]:
            number_of_bars = len(x_values[benchmark])
            bar_area_percentage = 0.8
            w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
            x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)
            
            experiment_params = variant.split('+')[1:]

            #number_of_bars = 3
            
            if (lib not in list(x_values[benchmark][variant].keys())):
                print("Error in .yml file. Only anchor_encr_scone lib is supported")
                exit()            

            labels.append(experiment_params[0].split('type=')[1].replace("_non_opt", " w/o Opt"))

            x_index = np.arange(0, len(x_values[benchmark][variant][lib]), 1) + x_axis_spacing[space_idx]

            print(experiment_params)
            formatted_values = ["%.2f" % float(value) for value in y_values[benchmark][variant][lib]]
            print(str(lib) + " " + str(x_values[benchmark][variant][lib]) + " throughput : " + str(formatted_values))
            
            rect = ax.bar(x_index, [float(i) for i in y_values[benchmark][variant][lib]], width = w, 
                                    color = colour[space_idx], hatch = hatch[space_idx], edgecolor = 'black', align='center', label=labels[-1])
            space_idx = space_idx + 1
            #configure the look of the plot
            custom_x_ticks = list(map(float,x_values[benchmark][variant][lib]))
            custom_x_ticks = [round(a) for a in custom_x_ticks]
            plt.xticks(range(0,len(x_values[benchmark][variant][lib])), custom_x_ticks)

            for tick in ax.xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax.yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            if (metrics[y_axis_label] == "ops-per-second[1/sec]"):
                ax.set_ylabel("Ops/sec", fontsize=10)
            else:
                ax.set_ylabel(metrics[y_axis_label], fontsize=8)
            #ax.set_ylabel(metrics[y_axis_label], fontsize=8)
            ax.set_xlabel("Get ratio (%)", fontsize=8)

        lgd = ax.legend(loc='upper center', bbox_to_anchor=(0., 1.1, 1., .102), #loc='lower left',
                                    ncol=len(labels), borderaxespad=0., fontsize=8, handlelength=1.5, columnspacing= 0.3)

        plot_dir = plot_folder + "/" + benchmark
        plot_file_path = plot_dir + "/optimization_" + bench_info[x_axis_label]
        save_plot(plot_dir, plot_file_path, fig, lgd)
        plt.close(fig) 
        plt.show()
    return