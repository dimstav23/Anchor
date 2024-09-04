import os
import sys
import pandas as pd

def main() -> None:
    op_file = open("./results/anchor_encr/anchor_pmembench_map_breakdown_result", "r")

    hit_ratio_init = []
    reads_init = []
    data_structure = []
    for line in op_file:
        if line.startswith("shortcut"):
            hit_ratio_init.append(line.split()[2])
        elif line.startswith("raw"):
            reads_init.append(line.split()[3])
        elif "ctree" in line:
            data_structure.append("ctree")
        elif "rbtree" in line:
            data_structure.append("rbtree")
        elif "btree" in line:
            data_structure.append("btree")        
        elif "rtree" in line:
            data_structure.append("rtree")
        elif "hashmap_tx" in line:
            data_structure.append("hashmap")
    
    # print(hit_ratio_init)
    # print(reads_init)
    # print(data_structure)
    ctree_idx = [i for i, x in enumerate(data_structure) if x == "ctree"]
    rbtree_idx = [i for i, x in enumerate(data_structure) if x == "rbtree"]
    btree_idx = [i for i, x in enumerate(data_structure) if x == "btree"]
    rtree_idx = [i for i, x in enumerate(data_structure) if x == "rtree"]
    hashmap_idx = [i for i, x in enumerate(data_structure) if x == "hashmap"]
    
    hit_ratio = {'ctree': 0, 'rbtree': 0, 'btree': 0, 'rtree': 0, 'hashmap': 0}
    reads = {'ctree': 0, 'rbtree': 0, 'btree': 0, 'rtree': 0, 'hashmap': 0}
    for idx in ctree_idx:
        hit_ratio["ctree"] = hit_ratio["ctree"] + float(hit_ratio_init[idx])
        reads["ctree"] = reads["ctree"] + float(reads_init[idx])

    for idx in rbtree_idx:
        hit_ratio["rbtree"] = hit_ratio["rbtree"] + float(hit_ratio_init[idx])
        reads["rbtree"] = reads["rbtree"] + float(reads_init[idx])

    for idx in btree_idx:
        hit_ratio["btree"] = hit_ratio["btree"] + float(hit_ratio_init[idx])
        reads["btree"] = reads["btree"] + float(reads_init[idx])

    for idx in rtree_idx:
        hit_ratio["rtree"] = hit_ratio["rtree"] + float(hit_ratio_init[idx])
        reads["rtree"] = reads["rtree"] + float(reads_init[idx])

    for idx in hashmap_idx:
        hit_ratio["hashmap"] = hit_ratio["hashmap"] + float(hit_ratio_init[idx])
        reads["hashmap"] = reads["hashmap"] + float(reads_init[idx])
    
    print("-------------------------------------------------------------")
    print("| Index            | ctree | btree | rbtree | rtree | hashmap |")
    print("-------------------------------------------------------------")
    print("| Object reads (M) | " + "{:5.2f}".format(reads["ctree"]/len(ctree_idx)/1000000) + " |" + "{:6.2f}".format(reads["btree"]/len(btree_idx)/1000000) + " |" + "{:7.2f}".format(reads["rbtree"]/len(rbtree_idx)/1000000) + " |" + "{:6.2f}".format(reads["rtree"]/len(rtree_idx)/1000000) + " |" + "{:8.2f}".format(reads["hashmap"]/len(hashmap_idx)/1000000) + " |")
    print("| Hit ratio (%)    | " + "{:5.2f}".format(hit_ratio["ctree"]/len(ctree_idx)) + " |" + "{:6.2f}".format(hit_ratio["btree"]/len(btree_idx)) + " |" + "{:7.2f}".format(hit_ratio["rbtree"]/len(rbtree_idx)) + " |" + "{:6.2f}".format(hit_ratio["rtree"]/len(rtree_idx)) + " |" + "{:8.2f}".format(hit_ratio["hashmap"]/len(hashmap_idx)) + " |")
    print("-------------------------------------------------------------")
    op_file.close()


if __name__ == "__main__":
    main()