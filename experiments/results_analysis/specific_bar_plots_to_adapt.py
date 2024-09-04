#BAR PLOT FOR TRANSACTION OPERATIONS
def bar_plot(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar plot")
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
            colour = ["royalblue", "grey", "salmon", "green", "darkgreen", "yellow"]
            #fig = plt.figure()
            #ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                internal_idx = list(x_values[benchmark][variant].keys()).index(version_lib)
                x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values[benchmark][variant][version_lib]], width = w, 
                                    color = colour[internal_idx], edgecolor = 'black', align='center', label=version_lib)
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
                                    ax[ax_idx].annotate('{:.2f}x'.format(percentage_change),
                                        xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                        xytext=(0, 3),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=5)
           
            #configure the look of the plot
            #plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            print(x_values[benchmark][variant][version_lib])
            ax[ax_idx].xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
            ax[ax_idx].xaxis.set_ticklabels(x_values[benchmark][variant][version_lib])
            #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
            for tick in ax[ax_idx].xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax[ax_idx].yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            ax[ax_idx].set_ylabel(metrics[y_axis_label], fontsize=10)
            ax[ax_idx].set_xlabel(bench_info[x_axis_label], fontsize=10)
            ax[ax_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            handles, labels = ax[ax_idx].get_legend_handles_labels()
            if (ax_idx == 1):
                lgd = ax[ax_idx].legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))          
            #save the plot
            #plot_dir = plot_folder + "/" + benchmark
            #plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
            #save_plot(plot_dir, plot_file_path, fig, lgd)
            #plt.close(fig) 
            ax_idx = ax_idx + 1

    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
    save_plot(plot_dir, plot_file_path, fig, lgd)
    plt.close(fig) 
    #plt.show()
    return

#BAR PLOT FOR DATA SIZE RESULTS
def bar_plot(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar plot")
    fig, ax = plt.subplots(2, 2)
    ax_x_idx = 0
    ax_y_idx = 0
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            #number_of_bars = 3
            number_of_bars = len(x_values[benchmark][variant])
            bar_area_percentage = 0.8
            w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
            x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)
            colour = ["royalblue", "grey", "salmon", "green", "darkgreen", "yellow"]
            #fig = plt.figure()
            #ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                internal_idx = list(x_values[benchmark][variant].keys()).index(version_lib)
                x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1) + x_axis_spacing[internal_idx]
                rect = ax[ax_x_idx][ax_y_idx].bar(x_index, [float(i) for i in y_values[benchmark][variant][version_lib]], width = w, 
                                    color = colour[internal_idx], edgecolor = 'black', align='center', label=version_lib)
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
                                    ax[ax_x_idx][ax_y_idx].annotate('{:.2f}x'.format(percentage_change),
                                        xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                        xytext=(0, 3),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=7)
           
            #configure the look of the plot
            #plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            ax[ax_x_idx][ax_y_idx].xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
            ax[ax_x_idx][ax_y_idx].xaxis.set_ticklabels(x_values[benchmark][variant][version_lib])
            #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
            for tick in ax[ax_x_idx][ax_y_idx].xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax[ax_x_idx][ax_y_idx].yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            ax[ax_x_idx][ax_y_idx].set_ylabel(metrics[y_axis_label], fontsize=10)
            ax[ax_x_idx][ax_y_idx].set_xlabel(bench_info[x_axis_label], fontsize=10)
            ax[ax_x_idx][ax_y_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            handles, labels = ax[ax_x_idx][ax_y_idx].get_legend_handles_labels()
            if (ax_y_idx == 1 and ax_x_idx == 1):
                lgd = ax[ax_x_idx][ax_y_idx].legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))          
            #save the plot
            #plot_dir = plot_folder + "/" + benchmark
            #plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
            #save_plot(plot_dir, plot_file_path, fig, lgd)
            #plt.close(fig) 
            ax_y_idx += 1
            if (ax_y_idx == 2):
                ax_x_idx = 1
                ax_y_idx = 0

    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
    save_plot(plot_dir, plot_file_path, fig, lgd)
    plt.close(fig) 
    #plt.show()
    return

#LINE PLOT FOR THREAD RESULTS
def line_plot_th(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("line plot")
    fig, ax = plt.subplots(2, 2)
    ax_x_idx = 0
    ax_y_idx = 0
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
            number_of_lines = len(x_values[benchmark][variant])
            colour = ["blue", "red", "salmon", "green", "darkgreen"]

            #fig = plt.figure()
            #ax = fig.add_subplot(111)

            #append values to the plot
            for version_lib in x_values[benchmark][variant]:     
                internal_idx = list(x_values[benchmark][variant].keys()).index(version_lib)
                x_index = np.arange(0, len(x_values[benchmark][variant][version_lib]), 1)
                y_index = [float(i) for i in y_values[benchmark][variant][version_lib]]
                line = ax[ax_x_idx][ax_y_idx].plot(x_index, y_index, marker='o', linestyle='dashed', markersize=6, label=version_lib)

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
                                    ax[ax_x_idx][ax_y_idx].annotate('{:.2f}x'.format(percentage_change),
                                        xy=(j, line_values[j]),
                                        xytext=(0, 5),  # 3 points vertical offset
                                        textcoords="offset points",
                                        ha='center', va='bottom', size=8)
                            
            #configure the look of the plot
            #plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            ax[ax_x_idx][ax_y_idx].xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
            ax[ax_x_idx][ax_y_idx].xaxis.set_ticklabels(x_values[benchmark][variant][version_lib])
            for tick in ax[ax_x_idx][ax_y_idx].xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax[ax_x_idx][ax_y_idx].yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            ax[ax_x_idx][ax_y_idx].set_ylabel(metrics[y_axis_label], fontsize=10)
            ax[ax_x_idx][ax_y_idx].set_xlabel(bench_info[x_axis_label], fontsize=10)
            ax[ax_x_idx][ax_y_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            handles, labels = ax[ax_x_idx][ax_y_idx].get_legend_handles_labels()
            if (ax_y_idx == 1 and ax_x_idx == 1):
                lgd = ax[ax_x_idx][ax_y_idx].legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))      
            
            
            #save the plot
            #plot_dir = plot_folder + "/" + benchmark
            #plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
            #save_plot(plot_dir, plot_file_path, fig, lgd)
            #plt.close(fig)
            ax_y_idx += 1
            if (ax_y_idx == 2):
                ax_x_idx = 1
                ax_y_idx = 0

    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
    save_plot(plot_dir, plot_file_path, fig, lgd)
    plt.close(fig) 
    #plt.show()
    return


#OVERHEAD BAR PLOT FOR INSERT/UPDATE/REMOVE 500K-1M
def bar_overhead_plot(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
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
                colour = ["grey", "black", "green", "red", "darkgreen", "yellow"]
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
                                            color = colour[internal_idx], edgecolor = 'black', align='center', label=version_lib)
                    
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
                colour = ["grey", "black", "green", "red", "darkgreen", "yellow"]
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
                                            color = colour[internal_idx], edgecolor = 'black', align='center', label=version_lib)
                    
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
def bar_overhead_plot(x_values, y_values, info, x_axis_label, y_axis_label, plot_folder):
    print("bar overhead plot")
    fig1, ax1 = plt.subplots(1, 3)
    ax_idx1 = 0
    fig2, ax2 = plt.subplots(1, 3)
    ax_idx2 = 0
    
    for benchmark in x_values:
        plot_title = benchmark
        for variant in x_values[benchmark]:
            experiment_params = variant.split('+')[1:]
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
            colour = ["grey", "black", "green", "red", "darkgreen", "yellow"]
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
                    rect = ax.bar(x_index, values_to_plot, width = w, 
                                        color = colour[internal_idx], edgecolor = 'black', align='center', label=version_lib)
                
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
                                        ha='center', va='bottom', size=5)

            #configure the look of the plot
            plt.xticks(range(0,len(x_values[benchmark][variant][version_lib])), x_values[benchmark][variant][version_lib])
            ax.xaxis.set_ticks(range(0,len(x_values[benchmark][variant][version_lib])))
            ax.xaxis.set_ticklabels(x_values[benchmark][variant][version_lib])
            #plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
            for tick in ax.xaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            for tick in ax.yaxis.get_major_ticks():
                tick.label.set_fontsize(8)
            #ax.set_ylabel(metrics[y_axis_label], fontsize=10)
            ax.set_ylabel("Relative throughput overhead w.r.t. pmdk native", fontsize=10)
            ax.set_xlabel(bench_info[x_axis_label], fontsize=10)
            #ax.set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
            ax.set_title(' '.join(map(str, experiment_params)), fontsize=10)
            handles, labels = ax.get_legend_handles_labels()
            
            #save the plot
            #plot_dir = plot_folder + "/" + benchmark
            #plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params))
            #save_plot(plot_dir, plot_file_path, fig, lgd)
            #plt.close(fig)
            #print(experiment_params)
            if (experiment_params[0] == 'total-ops=10000000' and experiment_params[2] == 'keys=100000'):
                if (ax_idx1 == 1):
                    lgd1 = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))
                ax1[ax_idx1] = ax
                ax_idx1 = ax_idx1 + 1
            elif (experiment_params[0] == 'total-ops=10000000' and experiment_params[2] == 'keys=500000'):
                if (ax_idx2 == 1):
                    lgd2 = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))
                ax2[ax_idx2] = ax
                ax_idx2 = ax_idx2 + 1
            else:
                lgd = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))
                plt.close(fig)
    plot_dir = plot_folder + "/" + benchmark
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params)) + "100k"
    save_plot(plot_dir, plot_file_path, fig1, lgd1)     
    plot_file_path = plot_dir + "/" + bench_info[x_axis_label] + "_" + '_'.join(map(str, experiment_params)) + "500k"
    save_plot(plot_dir, plot_file_path, fig2, lgd2)     
    return