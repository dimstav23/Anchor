# Result analysis scripts

`runner`: runs benchmarks with a specific configuration and saves their results in the indicated folder:
```
python3 runner.py <library version directory> <executable> <benchmark configuration folder> <result folder prefix> <name of the specific library version> <name of the config to run without .cfg>
```
example:
``` 
python3 runner.py /home/dimstav23/Desktop/Anchor-Code/src/nondebug/ pmembench /home/dimstav23/Desktop/Anchor-Code/src/benchmarks /home/dimstav23/Desktop/Anchor-Code/src/benchmarks anchor_mmap anchor_pmembench_tx_meaningful
```

`parser`: Parses the benchmark results and returns a dictionary containing them in a specific structure
parse(benchmark_configs, result_dir)
example:
parse(anchor_pmembench_tx_meaningful, /home/dimstav23/Desktop/Anchor-Code/src/benchmarks/library_version/anchor_pmembench_tx_meaningful_results)

`plotter`: contains analysis & plot functions (bar plots). Takes as input the dictionaries formed at the parser
see functions:
analyse_results(accumulated_results, config_to_plot, variants, metrics, info_idx, labels_idx, values_idx)
bar_plots(x_values, y_values, config_to_plot, variants, metrics, results_tags, info, save_flag, general_plot_dir)

`bar_plotter`: takes a configuration and plots the respective bar plots based on the accumulated results
config_to_plot variable should be set according to the name of the benchmark scenarios that should be plotted and based on which value
```
python3 bar_plotter.py <library version 1> <library version 2> <library version 3>
```
example:
```
python3 bar_plotter.py anchor_mmap anchor_no_tmr_thread pmdk
```

System setup:
2MB or 1GB pages should be enabled to avoid pagefaults due to the manifest (https://wiki.debian.org/Hugepages)

To check the stats: `cat /proc/meminfo | grep Huge`

In case you need to create the `hugetlbfs` (see also [here](https://stackoverflow.com/questions/28823878/how-to-mount-the-huge-tlb-huge-page-as-a-file-system)):
```
mkdir /dev/hugepages or /mnt/pmem/hugepages
mount -t hugetlbfs nodev /dev/hugepages or /mnt/pmem/hugepages (/mnt/pmem should be dax mounted)
```