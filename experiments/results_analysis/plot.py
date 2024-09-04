import os
import numpy as np
import matplotlib.pyplot as plt
from analyse import metrics,bench_info
from plotters import bar_plot, line_plot, bar_overhead_plot, bar_plot_tx_op, bar_plot_tx_op_overhead, bar_plot_ds, line_plot_th, \
                     bar_overhead_plot_common, bar_overhead_plot_custom, bar_plot_opt

def plot(plot_config, x_values, y_values, info):
    plot_type = plot_config['plot']['plot_type']
    plot_folder = plot_config['plot']['plot_folder']
    x_axis_label = plot_config['plot']['x_axis']
    y_axis_label = plot_config['plot']['y_axis']

    if plot_type=="bar":
        plot_fun = bar_plot
    elif plot_type=="line":
        plot_fun = line_plot
    elif plot_type=="bar_overhead_common":
        plot_fun = bar_overhead_plot_common
    elif plot_type=="bar_overhead_custom":
        plot_fun = bar_overhead_plot_custom
    elif plot_type=="bar_tx_op":
        plot_fun = bar_plot_tx_op
    elif plot_type=="bar_tx_op_overhead":
        plot_fun = bar_plot_tx_op_overhead
    elif plot_type=="bar_ds":
        plot_fun = bar_plot_ds
    elif plot_type=="line_th":
        plot_fun = line_plot_th
    elif plot_type=="bar_opt":
        plot_fun = bar_plot_opt

    plot_x = {}
    plot_y = {}
    plot_info = {}
    
    for benchmark in x_values:
        plot_x[benchmark] = {}
        plot_y[benchmark] = {}
        plot_info[benchmark] = {}
        for lib_version in x_values[benchmark]:
            for variant in x_values[benchmark][lib_version]:
                if (variant not in plot_x[benchmark]):
                    plot_x[benchmark][variant] = {}
                    plot_y[benchmark][variant] = {}
                    plot_info[benchmark][variant] = {}
                plot_x[benchmark][variant][lib_version] = x_values[benchmark][lib_version][variant]
                plot_y[benchmark][variant][lib_version] = y_values[benchmark][lib_version][variant]
                plot_info[benchmark][variant][lib_version] = info[benchmark][lib_version][variant]
    
    plot_fun(plot_x, plot_y, plot_info, x_axis_label, y_axis_label, plot_folder)

    return