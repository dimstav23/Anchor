import subprocess
import os
import sys

from acc_results import acc_results
from acc_results_breakdown import acc_results_breakdown
from acc_results_networking import acc_results_networking
from synth_map_results import synth_map_results
from plotters import create_dir

def main() -> None:
    repeats = 3
    pwd = os.getcwd()
    print("****** Gethering results ******")
    create_dir(pwd + "/results/")
    os.system("cp -r " + pwd + "/../../results/* " + pwd + "/results/")
    os.system("cp -r " + pwd + "/../../scone/results/* " + pwd + "/results/")
    print("****** Accummulating results ******")
    acc_results(pwd + "/results/", repeats)
    print("****** Accummulating breakdown results ******")
    acc_results_breakdown(pwd + "/results/", repeats)
    print("****** Accummulating networking results ******")
    acc_results_networking(pwd + "/results/", repeats)
    print("****** Map results synthesis ******")
    synth_map_results(pwd + "/results/")

if __name__ == "__main__":
    main()