import os
import numpy as np
import matplotlib.pyplot as plt

def create_dir(new_dir):
    if not(os.path.exists(new_dir)):
        try:
            os.mkdir(new_dir)
        except OSError as error:
            print ("Creation of the directory %s failed : %s" % (new_dir, error))
        else:
            print ("Successfully created the directory %s" % new_dir)

def data_structures_init(config_to_plot, variants, metrics):
    #data structures init
    x_values = {}
    y_values = {}
    for variant in config_to_plot.keys():
        x_values[variant] = {}
        y_values[variant] = {}
        for benchmark in config_to_plot[variant]:
            x_values[variant][benchmark] = []
            y_values[variant][benchmark] = {}
            for metric in metrics.keys():
                y_values[variant][benchmark][metric] = []

    info = {}
    for info_variant in variants:
        info[info_variant] = {}
        for variant in config_to_plot.keys():
            for benchmark in config_to_plot[variant]:
                if (info_variant != variant):
                    info[info_variant][benchmark] = []

    return x_values, y_values, info

def analyse_results(accumulated_results, config_to_plot, variants, metrics, info_idx, labels_idx, values_idx):
    #data structures init
    x_values, y_values, info = data_structures_init(config_to_plot, variants, metrics)

    for variant in config_to_plot.keys():
        variants_for_info = [item for item in variants.keys() if item != variant]
        for benchmark in config_to_plot[variant]:
            for result_set in accumulated_results:
                experiments = result_set[benchmark][info_idx][1]

                #find configuration parameters different than those considered for the plot
                for info_variant in variants_for_info:
                    if (info_variant in result_set[benchmark][labels_idx]):
                        info_value_idx = result_set[benchmark][labels_idx].index(variants[info_variant])
                        info_value_temp_list = []
                        for i in range(int(experiments)):
                            info_value_temp_list.append(result_set[benchmark][values_idx][i][info_value_idx])
                        info[info_variant][benchmark].append(info_value_temp_list)

                #find index of the variant values
                x_value_idx = result_set[benchmark][labels_idx].index(variants[variant])  
                x_value_temp_list = []
                for i in range(int(experiments)):
                    x_value_temp_list.append(result_set[benchmark][values_idx][i][x_value_idx])
                x_values[variant][benchmark].append(x_value_temp_list)

                for metric in metrics.keys():
                    #find index of values of the respective metric values
                    y_value_idx = result_set[benchmark][labels_idx].index(metrics[metric])
                    y_value_temp_list = []
                    for i in range(int(experiments)):
                        y_value_temp_list.append(result_set[benchmark][values_idx][i][y_value_idx])
                    y_value_temp_list = [float(item) for item in y_value_temp_list]
                    y_values[variant][benchmark][metric].append(y_value_temp_list)

    return x_values, y_values, info

def save_plot(plot_save_dir, fig, lgd, configuration):
    plot_file_path = plot_save_dir + configuration + ".pdf"
    fig.savefig(plot_file_path, dpi=300, format='pdf', bbox_extra_artists=(lgd,), bbox_inches='tight')

def bar_plots(x_values, y_values, config_to_plot, variants, metrics, results_tags, info, save_flag, general_plot_dir):
    number_of_bars = len(results_tags)
    bar_area_percentage = 0.8
    w = float(bar_area_percentage / number_of_bars) #bar width to cover 80% of the dedicated space
    x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)
    colour = ["blue", "red", "salmon", "green", "darkgreen"]
    for variant in config_to_plot.keys():
        for benchmark in config_to_plot[variant]:
            for metric in metrics.keys():
                #get the experiment configuration info
                configuration= ""
                for info_variant in variants:
                    if (info_variant != variant):
                        if (info[info_variant][benchmark]!=[]):
                            #assert guarantees that they have the same parameter configuration apart from the inspected one
                            assert all(elem == info[info_variant][benchmark][0][0] for elem in info[info_variant][benchmark][0])
                            for i in range(len(results_tags)-1):
                                #print(info[info_variant][benchmark][i], info[info_variant][benchmark][i+1])
                                assert info[info_variant][benchmark][i] == info[info_variant][benchmark][i+1]
                            #add the first value, as it describes the respective configuration option for all the experiments
                            configuration = configuration + "_" + info_variant + "=" + str(info[info_variant][benchmark][0][0])
                configuration = configuration[1:]

                fig = plt.figure()
                ax = fig.add_subplot(111)
                for i in range(len(results_tags)):
                    x_index = np.arange(0, len(x_values[variant][benchmark][i]), 1) + x_axis_spacing[i]
                    rect = ax.bar(x_index, y_values[variant][benchmark][metric][i], width = w, color = colour[i], edgecolor = 'black', align='center', label=results_tags[i])                
                    if (metric=='throughput'):
                        for j in range(len(rect)):
                            bar_values = [item[j] for item in y_values[variant][benchmark][metric]]
                            percentage_change = float(max(bar_values)) / rect[j].get_height()#(rect[j].get_height() - max(bar_values)) / max(bar_values) * 100
                            height = rect[j].get_height()
                            if (percentage_change != 1):
                                ax.annotate('{:.2f}x'.format(percentage_change),
                                    xy=(rect[j].get_x() + rect[j].get_width() / 2, height),
                                    xytext=(0, 3),  # 3 points vertical offset
                                    textcoords="offset points",
                                    ha='center', va='bottom', size=5)

                #plot configuration
                plt.xticks(range(0,len(x_values[variant][benchmark][i])), x_values[variant][benchmark][i])
                for tick in ax.xaxis.get_major_ticks():
                    tick.label.set_fontsize(8)
                for tick in ax.yaxis.get_major_ticks():
                    tick.label.set_fontsize(8)
                ax.set_ylabel(metrics[metric], fontsize=12)
                ax.set_xlabel(variants[variant], fontsize=12)
                ax.set_title(benchmark)
                handles, labels = ax.get_legend_handles_labels()
                lgd = ax.legend(handles, labels, loc='upper center', bbox_to_anchor=(0.5,-0.12))
                
                #plot save or show
                if (save_flag):
                    plot_save_dir = general_plot_dir + benchmark + "__" + variant + "__" + metric + "/"
                    create_dir(plot_save_dir)
                    save_plot(plot_save_dir, fig, lgd, configuration)
                else:
                    plt.show()