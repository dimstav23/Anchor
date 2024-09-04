# ANCHOR AE README

### Getting the code:
```
git clone git@github.com:dimstav23/Anchor.git
git submodule update --init pmdk
```

### Dependencies and requirements:

We performed our experiments on machines running NixOS. The dependencies that are required to run the project natively are enclosed in the `default.nix` file that can be found in the root folder of the repository.
To enter in a shell having all the required libraries and tools, please run:
```
$ export NIXPKGS_ALLOW_UNFREE=1
$ nix-shell
```

As `Anchor` makes use of [hugepages](https://wiki.debian.org/Hugepages), it is required that a hugetlbfs is mounted on `/dev/hugepages` directory. Make sure that you have sufficient free hugepages in the system. For better performance, 2MB hugepages are preferred.
The minimum amount of hugepages (for the sample benchmarks) should be 2GB.

Furthermore, `Anchor` emulates PM with DRAM. More specifically, it uses the `/dev/shm/` directory for its PM files.
Therefore, make sure that there is sufficient space available for `Anchor`.

To check the stats for hugepages : `cat /proc/meminfo | grep Huge`

To [mount a hugetlbfs](https://stackoverflow.com/questions/28823878/how-to-mount-the-huge-tlb-huge-page-as-a-file-system):
```
$ mkdir /dev/hugepages or /mnt/pmem/hugepages
$ mount -t hugetlbfs nodev /dev/hugepages
```

### Compile, install and run the native versions:

For benchmarks setup:
```
$ ./setup.sh -e native -b benchmarks -a /path/to/Anchor/ -p /path/to/Anchor/pmdk/
```
For benchmarks run:
```
$ ./run_native.sh -e native -b benchmarks -a /path/to/Anchor/ -p /path/to/Anchor/pmdk/
```

For write amplification microbenchmark setup: 
```
$ ./setup.sh -e native -b write_amplification -a /path/to/Anchor/
```
For write amplification microbenchmark run: 
```
$ ./run_native.sh -e native -b write_amplification -a /path/to/Anchor/
```

For breakdown microbenchmark setup: 
```
$ ./setup.sh -e native -b breakdown -a /path/to/Anchor/ -p /path/to/Anchor/pmdk/
```
For breakdown microbenchmark run:
```
$ ./run_native.sh -e native -b breakdown -a /path/to/Anchor/ -p /path/to/Anchor/pmdk/
```

### Notes:
- If in any of the above executions, an `Operation not permitted` or `Permission denied` error occurs, make sure that you have sufficient rights on the `/dev/hugepages` and `/dev/shm` directory and that the files existing there having naming collisions with the ones that `Anchor` tries to create can be deleted without elevated priviledges (e.g. they are not created by a `root` user from inside a container after an `Anchor` + `SCONE` execution). In any other case, remove them using `sudo` and proceed again with the execution.
- Make sure you cleanup any previous installation for each of the above benchmark categories because there might be code leftovers that can cause issues. We recommend clean installs for each one. Example of the recommended flow can be found [here](./native_setup_test.sh).
- For more information about build flags, see also [here](./PMDK_README.md#anchor-build).
- The original trusted counter was adopted by the [SPEICHER](https://www.usenix.org/conference/fast19/presentation/bailleu) project. Here we provide a simulation of the trusted counter. For results similar with Anchor, you have to adjust the `LOOP_DELAY` variable in the code accordingly after performing measurements in your own execution environment.

# IMPORTANT: SCONE NOTE!!! 
For our experiments we used a research `SCONE` version which has not become publicly available.
Therefore, the specific `SCONE` runtime cannot be released without permission.
However, for completeness, we provide the instructions we used for our `SCONE` experiments, despite its unaivalability.

(*scone image tag used: 1scone-run-ubuntu-18.04` & `scone-run-ubuntu-18.04-scone4.2.1`*)

### SCONE setup:

To create the `SCONE` image with `Anchor` installed, navigate to `Anchor/scone/` directory, replace the initial scone image name with the one located in your machine and run:
```
$ cd /path/to/Anchor/scone/
$ ./build.sh
```

After you build the appropriate image, run:
```
$ ./run-command bash
```
This command creates a container with the appropriate mappings (**be aware if any paths needs to be changed**) and opens a shell.

To run the benchmarks inside `SCONE`, in the acquired shell run:
```
$ ./run_scone_benchmarks.sh 
```
Note that, this step includes the setup of the libraries and exports the necessary environment variables.
To perform it independently, you can run `./setup.sh -e scone -b benchmarks -a /usr/lib/x86_64-linux-gnu/`

# IMPORTANT: Result analysis NOTE!!!
We provide all our scripts used for our result analysis and plots for reference.
However, most of the scripts below require a complete set of results (w/ `SCONE` results) to function properly.
Therefore, there might be errors related to missing results.

### Analysing the results:

The aforementioned scripts will place the results in `/path/to/Anchor/results/` for the native benchmark execution and in `/path/to/Anchor/scone/results` for the benchmark execution in `SCONE`.
To gather the results:
```
$ cd /path/to/Anchor/experiments/results_analysis
$ python3 gather_results.py
```
This script will accumulate the results of the becnhmark runs, take the average, and place all the resulting CSVs in the `/path/to/Anchor/experiments/results_analysis/results` directory.

### Plots:
To generate the plots of the paper:
```
$ cd /path/to/Anchor/experiments/results_analysis
# figure 2: 
$ python3 generate_plots.py plot_config_maps.yml
# figure 3: 
$ python3 breakdown.py plot_config_breakdown.yml
# figure 4: 
$ python3 generate_plots.py plot_config_alloc_update_free.yml
# figure 5: 
$ python3 generate_plots.py plot_config_maps_optimization.yml
# figure 6: 
$ python3 generate_plots.py plot_config_KV_operations.yml
# figure 7: TODO - pending
# figure 8: TODO - pending
# figure 9: 
$ python3 networking_plot.py
```
The plots are generated in the created `/path/to/Anchor/experiments/results_analysis/plots` folder.

### Tables:
To generate the tables of the paper:
```
$ cd /path/to/Anchor/experiments/results_analysis
# table 2: 
$ python3 table2.py
# table 3 (currently taken from anchor_pmembench_map_breakdown_result --- fix the path):
$ python3 table3.py 
# table 4: 
$ python3 table4.py
# table 5: 
$ python3 table5.py
# table 6: 
$ python3 table6.py
```
The tables are gathered from the results and presented in stdout.

# Network experiments
For the network experiments, please see [here](NETWORKING.md).

# Directory structure
- [experiments](./experiments/): Directory with configuration files, running scripts and result analysis scripts for the Anchor benchmarking.
- [nix](./nix): Directory with `.nix` helper files for tool installation (some of them are unused).
- [pmdk](./pmdk/): PMDK for submodule for vanilla PMDK benchmarks.
- [scone](./scone): Helper scripts and Dockerfiles used to instatiate the `scone` runtime for our benchmarks.
- [src](./src): Adapted PMDK source code that includes the Anchor additions. Most of Anchor's functionalities live [here](./src/anchor/).
- [Makefile](./Makefile): `Makefile` that builds the project.
- [default.nix](./default.nix): `nix` file used to set up the appropriate environment.
- [library_version_setup.py](./library_version_setup.py): `python` script that sets up the required libraries (Anchor and PMDK) in specified directories based on its command line arguments. For more information, consult the [script](./library_version_setup.py).
- [run_native.sh](./run_native.sh): Shell script that automates the native execution of benchmarks (PMDK and Anchor variants) using the required parameters and repeats.
- [run_scone.sh](./run_scone.sh): (*Warning: Deprecated*) Shell script that automates the execution of benchmarks (PMDK and Anchor variants) in `scone`.
- [run_scone_benchmarks.sh](./run_scone_benchmarks.sh): Shell script that automates the execution of benchmarks (PMDK and Anchor variants) in `scone` using the required parameters and repeats.
- [setup.sh](./setup.sh): Shell script that automates the process of setting up the required libraries for the experiments with the required compile-time parameters. For more information, consult the [script](./setup.sh) and see the workflows above in the present document.
- [anchor-eRPC](./anchor-eRPC/): `eRPC` and `DPDK` code for `scone`` version for Anchor.
- [anchor_network](./anchor_network/): Sample [client](./anchor_network/client.cpp) and [server](./anchor_network/server.cpp) network applications for Anchor.
- [network-stack](./network-stack/): The designed secure network stack for Anchor based on `eRPC` and `DPDK`.
