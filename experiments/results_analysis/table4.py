import os
import sys
import pandas as pd

# to get average of a list
def average(lst):
    return sum(lst) / len(lst)

def main() -> None:
    op_file = open("./results/anchor_encr/extra_writes_result", "r")
    s = [ line for line in op_file if not (line.startswith("raw") or line.startswith("shortcut"))]
    op_file.close()
    header = s[0]
    values = s[1::2]
    
    temp_f = open("temp_csv", "w")
    temp_f.write(header)
    for value in values:
        temp_f.write(value)
    
    temp_f.close()
    extra_writes = pd.read_csv("temp_csv", delimiter=';', engine='python', skiprows=0)
    os.remove("temp_csv")

    extra_writes = extra_writes[["op_type", "MANIFEST", "UNDO_LOG_DATA", "UNDO_LOG_DATA_PMDK", 
                                "REDO_LOG_DATA", "REDO_LOG_DATA_PMDK", "tx_number", "objects_per_tx"]]
    
    alloc_pmdk_log = []
    update_pmdk_log = []
    free_pmdk_log = []

    alloc_anchor_log = []
    update_anchor_log = []
    free_anchor_log = []

    alloc_manifest = []
    update_manifest = []
    free_manifest = []
    
    alloc = extra_writes[extra_writes["op_type"]=="alloc"]
    update = extra_writes[extra_writes["op_type"]=="update"]
    free = extra_writes[extra_writes["op_type"]=="free"]

    # get the average per object per transaction
    for i in range(len(alloc)):
        objects = alloc["tx_number"][3*i] * alloc["objects_per_tx"][3*i]
        alloc_pmdk_log.append((alloc["UNDO_LOG_DATA_PMDK"][3*i] + alloc["REDO_LOG_DATA_PMDK"][3*i]) / objects)
        alloc_anchor_log.append((alloc["UNDO_LOG_DATA"][3*i] + alloc["REDO_LOG_DATA"][3*i]) / objects)
        alloc_manifest.append(alloc["MANIFEST"][3*i] / objects)
    
    for i in range(len(update)):
        objects = update["tx_number"][3*i+1] * update["objects_per_tx"][3*i+1]
        update_pmdk_log.append((update["UNDO_LOG_DATA_PMDK"][3*i+1] + update["REDO_LOG_DATA_PMDK"][3*i+1]) / objects)
        update_anchor_log.append((update["UNDO_LOG_DATA"][3*i+1] + update["REDO_LOG_DATA"][3*i+1]) / objects)
        update_manifest.append(update["MANIFEST"][3*i+1] / objects)

    for i in range(len(free)):
        objects = free["tx_number"][3*i+2] * free["objects_per_tx"][3*i+2]
        free_pmdk_log.append((free["UNDO_LOG_DATA_PMDK"][3*i+2] + free["REDO_LOG_DATA_PMDK"][3*i+2]) / objects)
        free_anchor_log.append((free["UNDO_LOG_DATA"][3*i+2] + free["REDO_LOG_DATA"][3*i+2]) / objects)
        free_manifest.append(free["MANIFEST"][3*i+2] / objects)

    print("---------------------------------------------------------------")
    print("|          |        PMDK       |             ANCHOR           |")
    print("|Operation | Undo/redo log (B) | Undo/redo log (B) | Manifest |")
    print("---------------------------------------------------------------")
    print("|tx_alloc  |" + "{:18.2f}".format(average(alloc_pmdk_log)) + " | " + "{:17.2f}".format(average(alloc_anchor_log)) + " | " + "{:8.2f}".format(average(alloc_manifest)) + " |")
    print("|tx_update |" + "{:18.2f}".format(average(update_pmdk_log)) + " | " + "{:17.2f}".format(average(update_anchor_log)) + " | " + "{:8.2f}".format(average(update_manifest)) + " |")
    print("|tx_free   |" + "{:18.2f}".format(average(free_pmdk_log)) + " | " + "{:17.2f}".format(average(free_anchor_log)) + " | " + "{:8.2f}".format(average(free_manifest)) + " |")
    print("---------------------------------------------------------------")
    
if __name__ == "__main__":
    main()