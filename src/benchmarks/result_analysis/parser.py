import os
from collections import namedtuple

def parse(benchmark_configs, result_dir):
    original_dir = os.getcwd()
    os.chdir(result_dir) #go to the directory where the results are located

    results = {} #dictionary for the results of all the benchmarks
    line_category = {    
        "info": True,
        "labels": False,
        "values": 0
    }

    for benchmark in benchmark_configs:
        result_file_path = result_dir + "/" + benchmark + "_result"
        print("Parsing starts for " + result_file_path )
        result_file = open(result_file_path, 'r')
        for line in result_file: 
            line = line.replace('\n','')
            if (line_category["info"]): #case of benchmark info line
                splitted_line = line.split()
                name = splitted_line[0][:-1] #remove ':'
                category = splitted_line[1]
                number_of_experiments = splitted_line[2][1:-1] #remove '[]'
                group = splitted_line[4][:-1] #remove ']'
                results[name] = []
                results[name].append([category, number_of_experiments, group])
                line_category["info"] = False
                line_category["labels"] = True
            elif (line_category["labels"]): #case of label line
                labels = line.split(';')
                results[name].append(labels)
                line_category["labels"] = False
                results[name].append([]) #init the list of the values      
            elif (int(line_category["values"]) < int(number_of_experiments)): #number of lines to be read for values
                values = line.split(';')
                results[name][2].append(values)
                line_category["values"] = int(line_category["values"]) + 1
                if (int(line_category["values"]) == int(number_of_experiments)): #new benchmark in the next line
                    line_category["info"] = True
                    line_category["labels"] = False
                    line_category["values"] = 0
        
    os.chdir(original_dir) #get back to the original directory after the execution
    return results
