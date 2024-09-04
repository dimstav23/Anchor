import subprocess
import os
import sys
import yaml
from parser import parse
from analyse import filter_out, analyse
from plot import plot
import pandas as pd
import numpy as np
import math 

import os, csv, argparse
from collections import defaultdict

pd.options.mode.chained_assignment = None  # default='warn'

def acc_results(results_folder, repeats):

    results_path = results_folder
    number_of_experiments = repeats

    for subdir, dirs, files in os.walk(results_path):
        for dir in dirs:
            print("****** Processing directory " + results_path + dir + " ******")
            exp_list = []
            data = {}
            new_data = {}
            header = {}
            for file in os.listdir(results_path+dir):  
                if ((file.endswith("_1") or file.endswith("_2") or file.endswith("_3")) and 
                    ("breakdown" not in file) and (file[:-2] != "startup_50000_10") and
                    ("net") not in file):
                    # print(str(file))
                    if (file[:-2] not in exp_list):
                        print("****** Benchmark : " + file[:-2] + " ******")
                        if os.path.exists(results_path+dir+'/'+file[:-2]): 
                            os.remove(results_path+dir+'/'+file[:-2])
                        exp_list.append(file[:-2])
                        op_file = open(results_path+dir+'/'+file, "r")
                        header[file[:-2]] = op_file.readline()
                        op_file.close()
                        if (file.startswith("startup")):
                            data[file[:-2]] = pd.read_csv(results_path+dir+'/'+file, delimiter=';', engine='python', skiprows=0, header=None)
                        else:
                            data[file[:-2]] = pd.read_csv(results_path+dir+'/'+file, delimiter=';', engine='python', skiprows=1, header=None)
                    else:
                        # print(file + "  " + file[:-2])
                        if (file.startswith("startup")):
                            new_data[file[:-2]] = pd.read_csv(results_path+dir+'/'+file, delimiter=';', engine='python', skiprows=0, header=None)
                        else:
                            new_data[file[:-2]] = pd.read_csv(results_path+dir+'/'+file, delimiter=';', engine='python', skiprows=1, header=None)
                        
                        for i in list(data[file[:-2]].keys()):
                            for idx in range(len(data[file[:-2]][i])):
                                try:
                                    data[file[:-2]][i][idx] = float(data[file[:-2]][i][idx]) + float(new_data[file[:-2]][i][idx])
                                except(ValueError):
                                    data[file[:-2]][i] = data[file[:-2]][i]
                                except(TypeError):
                                    data[file[:-2]][i] = data[file[:-2]][i]

            for experiment in exp_list:
                for i in list(data[experiment].keys()):
                    for idx in range(len(data[experiment][i])):
                        try:
                            data[experiment][i][idx] = format(data[experiment][i][idx]/number_of_experiments,".6f")
                            if (int(float(data[experiment][i][idx])) == float(data[experiment][i][idx])):
                                data[experiment][i][idx] = int(float(data[experiment][i][idx]))
                        except(TypeError):
                            data[experiment][i][idx] = data[experiment][i][idx]
            
            for experiment in exp_list:
                if (not experiment.endswith("startup_50000_10")):
                    data[experiment].to_csv(results_path+dir+'/'+experiment+"_temp",sep=';', index=False, index_label=False, header=None)
                    # print(data[experiment])
                    if (experiment.startswith("startup")):
                        open(results_path+dir+'/'+experiment, "w").write(open(results_path+dir+'/'+experiment+"_temp").read())
                    else:
                        open(results_path+dir+'/'+experiment, "w").write(header[experiment] + open(results_path+dir+'/'+experiment+"_temp").read())
                    os.remove(results_path+dir+'/'+experiment+"_temp")
