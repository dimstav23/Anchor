import subprocess
import os
import sys
import yaml
from subprocess import Popen, PIPE

def save_keys(dest_file, keys):
    print(dest_file)
    file = open(dest_file, "w+")
    for key in keys:
        file.write(key + "\n")
    file.close()

def export_keys(file):
    f = open(file, 'r' )
    key_collection = set()
    for line in f.readlines():
        key = line.split(' ')[1].rstrip()
        key_collection.add(key)
        
    return key_collection

if (len(sys.argv) < 2):
    print("Usage: python3 export_traces_keys.py /path/to/traces/dir")
    exit()
else:
    trace_dir = sys.argv[1]

print(trace_dir)

for root, subdirs, files in os.walk(trace_dir):
    for file in files:
        keys = export_keys(root+'/'+file)
        dest_file = root + '/' + file.split('.txt')[0] + "_keys.txt"
        save_keys(dest_file, keys)

exit()


