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

def acc_results_networking(results_folder, repeats):

    results_path = results_folder
    number_of_experiments = repeats

    for subdir, dirs, files in os.walk(results_path):
        for dir in dirs:
            print("****** Processing directory " + results_path + dir + " ******")
            data = {}
            
            for file in os.listdir(results_path+dir):  
                if ((file.endswith("_1") or file.endswith("_2") or file.endswith("_3")) and ("net" in file)):
                    # print(str(file))
                    
                    data[file] = {}
                    # print(file + "  " + file[:-2])
                    if os.path.exists(results_path+dir+'/'+file[:-2]): 
                        os.remove(results_path+dir+'/'+file[:-2])

                    op_file = open(results_path+dir+'/'+file, "r")
                        
                    s = op_file.read()
                    s = s.split("Ended experiment\n")[1]
                    data[file] = re.findall(r"[+]?\d*\.\d+|\d+", s) # find all the numbers in the file
                    data[file] = [float(x) for x in data[file]] # convert them to float

                    op_file.close()
            
            new_data = {}
            if len(data) != 0:
                for fname, numbers in data.items():
                    if fname[:-2] not in new_data.keys():
                        new_data[fname[:-2]] = numbers
                    else:
                        new_data[fname[:-2]] = [a + b for a, b in zip(new_data[fname[:-2]], numbers)]
                
                for fname in new_data.keys():
                    new_data[fname] = [format(x / number_of_experiments,".6f").rstrip('0').rstrip('.') for x in new_data[fname]] # take the avg
                    op_file = open(results_path+dir+'/'+fname+"_1", "r") #read the first file of each category to find the indexes
                    s = op_file.read()
                    s = s.split("Ended experiment\n")[1]
                    iter = re.finditer(r"[+]?\d*\.\d+|\d+", s) # find the numbers' indexes
                    op_file.close()

                    acc_file = s
                    idx = 0
                    update_idx = 0
                    for match in iter:
                        # print(acc_file[match.start()+update_idx:match.end()+update_idx])
                        acc_file = acc_file[0:match.start()+update_idx] + str(new_data[fname][idx]) + acc_file[match.end()+update_idx:] # replace numbers with avg
                        update_idx += len(str(new_data[fname][idx])) - (match.end() - match.start()) # update the index based on the new value length
                        idx += 1

                    # print(acc_file)
                    print("****** Benchmark : " + fname + " ******")
                    open(results_path+dir+'/'+ fname , "w").write(acc_file)
            