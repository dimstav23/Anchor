import subprocess
import os
import sys
import yaml
from subprocess import Popen, PIPE

def create_dir(dir_path):
    try:
        os.mkdir(dir_path)
    except OSError as error:
        print ("Creation of the directory %s failed : %s" % (dir_path, error))
    else:
        print ("Successfully created the directory %s" % dir_path)

def run(benchmark_program, benchmark_configs, result_folder_path):
    create_dir(result_folder_path)
    for benchmark in benchmark_configs:
        print("running " + benchmark)
        outfile = result_folder_path+ "/" + benchmark + "_result"
        cmd = benchmark_program + " " + benchmark + ".cfg > " + outfile
        #print(cmd)
        #os.popen(cmd)
        #exit()
        p = subprocess.Popen(cmd, stdout=subprocess.PIPE, shell=True)
        (output, err) = p.communicate()
        #This makes the wait possible
        p_status = p.wait()

if (len(sys.argv) < 2):
    print("Usage: python3 run_experiments.py /path/to/config_file.yml")
    exit()
else:
    cfg_file = sys.argv[1]

config = yaml.safe_load(open(cfg_file))
for elem in config:
    library_version = elem['cfg']['library_version']
    library_path = elem['cfg']['library_path']
    benchmark_program = elem['cfg']['benchmark_program']
    benchmark_path = elem['cfg']['benchmark_path']
    result_path = elem['cfg']['result_path']
    benchmark_configs = elem['cfg']['benchmark_configs']

    preload_libs=["/libanchor.so:", "/libpmem.so.1:", "/libpmemobj.so.1:", "/libpmemlog.so.1:", "/libpmemblk.so.1:", "/libpmempool.so.1:", "/librpmem.so.1:"]
    preload_libs=[library_path + lib for lib in preload_libs]
    preload_libs.append("/home/sconeWorkspace/downloads/gperftools/build/lib/libprofiler.so.0")

    os.environ['PRELOAD_LIBS'] = "".join(preload_libs)

    benchmark_program = "LD_PRELOAD=$PRELOAD_LIBS Hugepagesize=2097152\
                        LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/7/ SCONE_VERSION=1\
                        SCONE_LOG=0 SCONE_NO_FS_SHIELD=1 SCONE_NO_MMAP_ACCESS=1 SCONE_HEAP=3584M SCONE_LD_DEBUG=1\
                        /opt/scone/lib/ld-scone-x86_64.so.1" + " ./" + benchmark_program
    #result_folder_suffix = "results"
    create_dir(result_path)
    result_folder_path = result_path + "/" + library_version #+ '_' + result_folder_suffix

    original_dir = os.getcwd()
    os.chdir(benchmark_path) #go to the directory where the benchmarks are located
    print("Version " + library_version)
    run(benchmark_program, benchmark_configs, result_folder_path)

    os.chdir(original_dir) #get back to the original directory after the execution

exit()