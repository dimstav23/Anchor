import subprocess
import os
import sys
from subprocess import Popen, PIPE

'''
Usage :  python3 library_version_setup.py <directory where the libraries install folder should be put> <"SCONE"-if build for scone>
'''

def print_usage():
    print("Usage :  python3 library_version_setup.py <directory where the libraries install folder should be put> \
\n\t<SCONE-if build for scone> \
\n\t<STATS - if statistics versions should be added>\
\n\t<WRITE_AMPL - if write_amplification version should be installed>")
    quit()

if len(sys.argv) < 2 or len(sys.argv) > 6:
    print_usage()
else:
    scone_flag = ""
    stats_flag = ""
    write_ampl_flag = ""
    recovery_flag = ""
    install_path = sys.argv[1]

    if (len(sys.argv) >= 3):
        if (sys.argv[2] == "SCONE"):
            scone_flag = sys.argv[2]
        elif (sys.argv[2] == "STATS"):
            stats_flag = sys.argv[2]
        elif (sys.argv[2] == "WRITE_AMPL"):
            write_ampl_flag = sys.argv[2]

    if (len(sys.argv) >= 4):
        if (sys.argv[3] == "SCONE"):
            scone_flag = sys.argv[3]
        elif (sys.argv[3] == "STATS"):
            stats_flag = sys.argv[3]
        elif (sys.argv[3] == "WRITE_AMPL"):
            write_ampl_flag = sys.argv[3]

    if (len(sys.argv) >= 5):
        if (sys.argv[4] == "SCONE"):
            scone_flag = sys.argv[4]
        elif (sys.argv[4] == "STATS"):
            stats_flag = sys.argv[4]
        elif (sys.argv[4] == "WRITE_AMPL"):
            write_ampl_flag = sys.argv[4]

    if (len(sys.argv) >= 6):
        if (sys.argv[5] == "SCONE"):
            scone_flag = sys.argv[5]
        elif (sys.argv[5] == "STATS"):
            stats_flag = sys.argv[5]
        elif (sys.argv[5] == "WRITE_AMPL"):
            write_ampl_flag = sys.argv[5]

clean_cmd = "make clean && make clean ANCHOR_FUNCS=1"
make_cmd = "make ANCHOR_FUNCS=1"
install_cmd = "make install ANCHOR_FUNCS=1"

flags = ["ENCR_OFF=1",
         "ENCR_OFF=0"]

if (scone_flag == "SCONE"):
    flags = [flag + " SCONE=1" for flag in flags] 

if (stats_flag == "STATS"):
    flags = [flag + " STATISTICS=1 " for flag in flags] 

if (write_ampl_flag == "WRITE_AMPL"):
    flags = [flag + " WRITE_AMPL=1 " for flag in flags] 

prefixes = [install_path+"/anchor_no_encr",
          install_path+"/anchor_encr"]

if (scone_flag == "SCONE"):
    prefixes = [prefix + "_scone" for prefix in prefixes]

if (stats_flag == "STATS"):
    prefixes = [prefix + "_stats" for prefix in prefixes] 

if (write_ampl_flag == "WRITE_AMPL"):
    prefixes = [prefix + "_write_ampl" for prefix in prefixes]

for i in range(0, len(prefixes)):
    if not(os.path.exists(prefixes[i])):
        os.mkdir(prefixes[i])
    cmd = clean_cmd + " && " + make_cmd + " " + flags[i] + " && " + install_cmd + " " + flags[i] + " prefix=" + prefixes[i]
    print("Running " + cmd)
    p = subprocess.Popen(cmd, stderr=subprocess.STDOUT, shell=True)
    (output, err) = p.communicate()  
    #This makes the wait possible
    p_status = p.wait()
