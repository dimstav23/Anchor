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

def run(benchmark_program, benchmark_configs, result_folder_path, repeats):
    create_dir(result_folder_path)
    for benchmark in benchmark_configs:
        print("running " + benchmark)
        outfile = result_folder_path+ "/" + benchmark + "_result"
        for i in range(repeats):
            print("Repeat " + str(i+1))
            cmd = benchmark_program + " " + benchmark + ".cfg > " + outfile + "_" + str(i+1)
            #print(cmd)
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
    
    benchmark_program = "LD_LIBRARY_PATH=" + library_path + " ./" + benchmark_program
    
    #result_folder_suffix = "results"
    create_dir(result_path)
    result_folder_path = result_path + "/" + library_version #+ '_' +result_folder_suffix
    original_dir = os.getcwd()
    os.chdir(benchmark_path) #go to the directory where the benchmarks are located
    print("Version " + library_version)
    repeats = 3
    run(benchmark_program, benchmark_configs, result_folder_path, repeats)
    os.chdir(original_dir) #get back to the original directory after the execution

exit()


