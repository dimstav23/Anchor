import os
import sys
import pandas as pd

def main() -> None:
    startup_50000_10_2 = pd.read_csv("./results/anchor_encr_scone/startup_50000_10_2", delimiter=';', engine='python', skiprows=0)
    startup_50000_10_4 = pd.read_csv("./results/anchor_encr_scone/startup_50000_10_4", delimiter=';', engine='python', skiprows=0)
    startup_50000_10_8 = pd.read_csv("./results/anchor_encr_scone/startup_50000_10_8", delimiter=';', engine='python', skiprows=0)
    startup_50000_10_12 = pd.read_csv("./results/anchor_encr_scone/startup_50000_10_12", delimiter=';', engine='python', skiprows=0)

    print("------------------------------------------------------")
    print("|Manifest size (MiB) ||  96  |  138  |  224  |  266  |")
    print("------------------------------------------------------")
    print("|Recovery time (s)   ||" + "{:5.2f}".format(startup_50000_10_2["time_taken"][0]) + " |" + "{:6.2f}".format(startup_50000_10_4["time_taken"][0]) + " |" + "{:6.2f}".format(startup_50000_10_8["time_taken"][0]) + " |" + "{:6.2f}".format(startup_50000_10_12["time_taken"][0]) + " |")
    print("------------------------------------------------------")

if __name__ == "__main__":
    main()