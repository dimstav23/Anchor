import os
import sys
import pandas as pd

def main() -> None:
    pmdk_res = pd.read_csv("./results/pmdk/pmembench_rtree_small_result", 
                          delimiter=';', engine='python', skiprows=1)
    anchor_no_encr_res = pd.read_csv("./results/anchor_no_encr_scone/anchor_pmembench_map_rtree_small_result", 
                          delimiter=';', engine='python', skiprows=1)
    anchor_encr_res = pd.read_csv("./results/anchor_encr_scone/anchor_pmembench_map_rtree_small_result", 
                          delimiter=';', engine='python', skiprows=1)
    
    anchor_no_encr_50 = pmdk_res[pmdk_res["read-ratio"]==50]["ops-per-second[1/sec]"] / anchor_no_encr_res[anchor_no_encr_res["read-ratio"]==50]["ops-per-second[1/sec]"]
    anchor_no_encr_70 = pmdk_res[pmdk_res["read-ratio"]==70]["ops-per-second[1/sec]"] / anchor_no_encr_res[anchor_no_encr_res["read-ratio"]==70]["ops-per-second[1/sec]"]
    anchor_no_encr_90 = pmdk_res[pmdk_res["read-ratio"]==90]["ops-per-second[1/sec]"] / anchor_no_encr_res[anchor_no_encr_res["read-ratio"]==90]["ops-per-second[1/sec]"]

    anchor_encr_50 = pmdk_res[pmdk_res["read-ratio"]==50]["ops-per-second[1/sec]"] / anchor_encr_res[anchor_encr_res["read-ratio"]==50]["ops-per-second[1/sec]"]
    anchor_encr_70 = pmdk_res[pmdk_res["read-ratio"]==70]["ops-per-second[1/sec]"] / anchor_encr_res[anchor_encr_res["read-ratio"]==70]["ops-per-second[1/sec]"]
    anchor_encr_90 = pmdk_res[pmdk_res["read-ratio"]==90]["ops-per-second[1/sec]"] / anchor_encr_res[anchor_encr_res["read-ratio"]==90]["ops-per-second[1/sec]"]

    print("---------------------------------------------------------")
    print("|        Version          | 50% Get | 70% Get | 90% Get |")
    print("---------------------------------------------------------")
    print("|ANCHOR w/o Enc - 10k keys| " + "{:6.1f}x".format(anchor_no_encr_50[0]) + " | " + "{:6.1f}x".format(anchor_no_encr_70[1]) + " | " + "{:6.1f}x".format(anchor_no_encr_90[2])  + " |")
    print("|    ANCHOR - 10k keys    | " + "{:6.1f}x".format(anchor_encr_50[0]) + " | " + "{:6.1f}x".format(anchor_encr_70[1]) + " | " + "{:6.1f}x".format(anchor_encr_90[2])  + " |")
    print("---------------------------------------------------------")
    
if __name__ == "__main__":
    main()