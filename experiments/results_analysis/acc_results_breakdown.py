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
import re

import os, csv, argparse
from collections import defaultdict

pd.options.mode.chained_assignment = None  # default='warn'

def acc_results_breakdown(results_folder, repeats):

    results_path = results_folder
    number_of_experiments = repeats

    for subdir, dirs, files in os.walk(results_path):
        for dir in dirs:
            print("****** Processing directory " + results_path + dir + " ******")
            data = {}
            experiment = ''
            for file in os.listdir(results_path+dir):  
                if ((file.endswith("_1") or file.endswith("_2") or file.endswith("_3")) and ("breakdown" in file)):
                    # print(str(file))
                    experiment = file[:-2]
                    data[file] = {}
                    # print(file + "  " + file[:-2])
                    if os.path.exists(results_path+dir+'/'+file[:-2]): 
                        os.remove(results_path+dir+'/'+file[:-2])

                    op_file = open(results_path+dir+'/'+file, "r")
                        
                    s = op_file.read()
                    data[file] = re.findall(r"[+]?\d*\.\d+|\d+", s) # find all the numbers in the file
                    data[file] = [float(x) for x in data[file]] # convert them to float
                    iter = re.finditer(r"[+]?\d*\.\d+|\d+", s) # find the numbers' indexes

                    op_file.close()
            
            if len(data) != 0:
                new_data = []
                for fname, numbers in data.items():
                    if len(new_data) == 0:
                        new_data = numbers
                    else:
                        new_data = [a + b for a, b in zip(new_data, numbers)]
                new_data[:] = [format(x / number_of_experiments,".6f").rstrip('0').rstrip('.') for x in new_data] # take the avg
                # print(new_data)

                acc_file = s
                idx = 0
                update_idx = 0
                for match in iter:
                    # print(acc_file[match.start()+update_idx:match.end()+update_idx])
                    acc_file = acc_file[0:match.start()+update_idx] + str(new_data[idx]) + acc_file[match.end()+update_idx:] # replace numbers with avg
                    update_idx += len(str(new_data[idx])) - (match.end() - match.start()) # update the index based on the new value length
                    idx += 1

                # print(acc_file)
                print("****** Benchmark : " + experiment + " ******")
                open(results_path+dir+'/'+experiment, "w").write(acc_file)
            