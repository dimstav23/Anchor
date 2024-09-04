import os
import sys
import pandas as pd

def main() -> None:
    #skiprows = 1 to avoid lane_to_recover useless info
    recovery_50000_10_4_1000 = pd.read_csv("./results/anchor_encr_scone/recovery_50000_10_4_1000", delimiter=';', engine='python', skiprows=1)
    recovery_50000_10_4_5000 = pd.read_csv("./results/anchor_encr_scone/recovery_50000_10_4_5000", delimiter=';', engine='python', skiprows=1)
    recovery_50000_10_8_1000 = pd.read_csv("./results/anchor_encr_scone/recovery_50000_10_8_1000", delimiter=';', engine='python', skiprows=1)
    recovery_50000_10_8_5000 = pd.read_csv("./results/anchor_encr_scone/recovery_50000_10_8_5000", delimiter=';', engine='python', skiprows=1)

    print("----------------------------------------------------")
    print("| Manifest size (MiB) ||     138     |     224     |")
    print("----------------------------------------------------")
    print("| Log size (MiB)      || 0.98 | 4.88 | 0.98 | 4.88 |")
    print("----------------------------------------------------")
    print("| Recovery time (s)   ||" + "{:5.2f}".format(recovery_50000_10_4_1000["time_taken"][0]) + " |" + "{:5.2f}".format(recovery_50000_10_4_5000["time_taken"][0]) + " |" + "{:5.2f}".format(recovery_50000_10_8_1000["time_taken"][0]) + " |" + "{:5.2f}".format(recovery_50000_10_8_5000["time_taken"][0]) + " |")
    print("----------------------------------------------------")

if __name__ == "__main__":
    main()