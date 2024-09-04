import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import os

matplotlib.rcParams['pdf.fonttype'] = 42
matplotlib.rcParams['ps.fonttype'] = 42

versions_map = {
        "pmdk"                  :   "PMDK + eRPC w/o Enc",    
        "anchor_encr"           :   "Native Anchor w/ Enc",
        "anchor_no_encr"        :   "Native Anchor w/o Enc",
        "anchor_encr_scone"     :   "Anchor + Anchor-NS w/ Enc",
        "anchor_no_encr_scone"  :   "Anchor + Anchor-NS w/o Enc"
    }

colour = ["white", "black", "white", "grey", "white", "white"]
hatch = ['/' , '' , '' , '', '--', '++']
markers = ['o', 's', '+', 'x', 'D', '*']

def create_dir(new_dir):
    if not(os.path.exists(new_dir)):
        try:
            os.makedirs(new_dir)
        except OSError as error:
            print ("Creation of the directory %s failed : %s" % (new_dir, error))
        else:
            print ("Successfully created the directory %s" % new_dir)

def save_plot(plot_dir, plot_file_path, fig, lgd):
    create_dir(plot_dir)
    print(plot_file_path)
    fig.savefig(plot_file_path + ".pdf", dpi=300, format='pdf', bbox_extra_artists=(lgd,), bbox_inches='tight')
    fig.savefig(plot_file_path + ".png", dpi=300, format='png', bbox_extra_artists=(lgd,), bbox_inches='tight')

# Python program to get average of a list
def average(lst):
    return sum(lst) / len(lst)

# ./results/pmdk/net_hashmap_tx_50_512_result
pmdk_50_512 = [line.strip() for line in open("./results/pmdk/net_hashmap_tx_50_512_result", 'r')][1].split(";")[-1]
pmdk_90_512 = [line.strip() for line in open("./results/pmdk/net_hashmap_tx_90_512_result", 'r')][1].split(";")[-1]
pmdk_50_2048 = [line.strip() for line in open("./results/pmdk/net_hashmap_tx_50_2048_result", 'r')][1].split(";")[-1]
pmdk_90_2048 = [line.strip() for line in open("./results/pmdk/net_hashmap_tx_90_2048_result", 'r')][1].split(";")[-1]

# results/anchor_encr_scone/net_hashmap_tx_50_512_result
anchor_encr_scone_50_512 = [line.strip() for line in open("./results/anchor_encr_scone/net_hashmap_tx_50_512_result", 'r')][1].split(";")[-1]
anchor_encr_scone_90_512 = [line.strip() for line in open("./results/anchor_encr_scone/net_hashmap_tx_90_512_result", 'r')][1].split(";")[-1]
anchor_encr_scone_50_2048 = [line.strip() for line in open("./results/anchor_encr_scone/net_hashmap_tx_50_2048_result", 'r')][1].split(";")[-1]
anchor_encr_scone_90_2048 = [line.strip() for line in open("./results/anchor_encr_scone/net_hashmap_tx_90_2048_result", 'r')][1].split(";")[-1]

# results/anchor_no_encr_scone/net_hashmap_tx_50_512_result
anchor_no_encr_scone_50_512 = [line.strip() for line in open("./results/anchor_no_encr_scone/net_hashmap_tx_50_512_result", 'r')][1].split(";")[-1]
anchor_no_encr_scone_90_512 = [line.strip() for line in open("./results/anchor_no_encr_scone/net_hashmap_tx_90_512_result", 'r')][1].split(";")[-1]
anchor_no_encr_scone_50_2048 = [line.strip() for line in open("./results/anchor_no_encr_scone/net_hashmap_tx_50_2048_result", 'r')][1].split(";")[-1]
anchor_no_encr_scone_90_2048 = [line.strip() for line in open("./results/anchor_no_encr_scone/net_hashmap_tx_90_2048_result", 'r')][1].split(";")[-1]

number_of_bars = 3
bar_area_percentage = 0.8
w = float(bar_area_percentage / number_of_bars)
x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)

fig, ax = plt.subplots(1, 2)

fsize = 20

ax_idx = 0
x_labels = ['50%', '90%']
y_values_pmdk = [pmdk_50_512, pmdk_90_512]
y_values_anchor_no_encr = [anchor_no_encr_scone_50_512, anchor_no_encr_scone_90_512]
y_values_anchor_encr = [anchor_encr_scone_50_512, anchor_encr_scone_90_512]

x_index = np.arange(0, len(y_values_pmdk), 1) + x_axis_spacing[0]
rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values_pmdk], width = w, 
                                    color = colour[0], hatch = hatch[0],
                                    edgecolor = 'black', align='center', label="pmdk")
x_index = np.arange(0, len(y_values_anchor_no_encr), 1) + x_axis_spacing[1]
rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values_anchor_no_encr], width = w, 
                                    color = colour[1], hatch = hatch[1],
                                    edgecolor = 'black', align='center', label="anchor_no_encr_scone")
x_index = np.arange(0, len(y_values_anchor_encr), 1) + x_axis_spacing[2]
rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values_anchor_encr], width = w, 
                                    color = colour[2], hatch = hatch[2],
                                    edgecolor = 'black', align='center', label="anchor_encr_scone")

ax[ax_idx].xaxis.set_ticks(range(0,len(x_labels)))
ax[ax_idx].xaxis.set_ticklabels(x_labels)
ax[ax_idx].ticklabel_format(axis='y', scilimits=[-3, 3])
ax[ax_idx].yaxis.get_offset_text().set_fontsize(fsize-3)

#plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
for tick in ax[ax_idx].xaxis.get_major_ticks():
   tick.label.set_fontsize(fsize)
for tick in ax[ax_idx].yaxis.get_major_ticks():
    tick.label.set_fontsize(fsize)
if (ax_idx == 0):
    ax[ax_idx].set_ylabel("Ops/sec", fontsize=fsize)

ax[ax_idx].set_xlabel("Get ratio", fontsize=fsize)
#ax[ax_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
ax[ax_idx].set_title("Value size 512 bytes", fontsize=fsize-2)
handles, labels = ax[ax_idx].get_legend_handles_labels()
labels = [versions_map[label] for label in labels]
print(labels)
if (ax_idx == 0): #to set the legend once and in the middle
    lgd = ax[ax_idx].legend(handles, labels, loc='upper center', #bbox_to_anchor=(0.5,-0.12),
                            bbox_to_anchor=(0.535, 1.25, 1., .102), #loc='lower left',
                            ncol=2, borderaxespad=0., fontsize=fsize-3, columnspacing=0.3)          

ax_idx = 1
x_labels = ['50%', '90%']
y_values_pmdk = [pmdk_50_2048, pmdk_90_2048]
y_values_anchor_no_encr = [anchor_no_encr_scone_50_2048, anchor_no_encr_scone_90_2048]
y_values_anchor_encr = [anchor_encr_scone_50_2048, anchor_encr_scone_90_2048]

x_index = np.arange(0, len(y_values_pmdk), 1) + x_axis_spacing[0]
rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values_pmdk], width = w, 
                                    color = colour[0], hatch = hatch[0],
                                    edgecolor = 'black', align='center', label="pmdk")
x_index = np.arange(0, len(y_values_anchor_no_encr), 1) + x_axis_spacing[1]
rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values_anchor_no_encr], width = w, 
                                    color = colour[1], hatch = hatch[1],
                                    edgecolor = 'black', align='center', label="anchor_no_encr_scone")
x_index = np.arange(0, len(y_values_anchor_encr), 1) + x_axis_spacing[2]
rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values_anchor_encr], width = w, 
                                    color = colour[2], hatch = hatch[2],
                                    edgecolor = 'black', align='center', label="anchor_encr_scone")

ax[ax_idx].xaxis.set_ticks(range(0,len(x_labels)))
ax[ax_idx].xaxis.set_ticklabels(x_labels)
ax[ax_idx].ticklabel_format(axis='y', scilimits=[-3, 3])
ax[ax_idx].yaxis.get_offset_text().set_fontsize(fsize-3)

#plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
for tick in ax[ax_idx].xaxis.get_major_ticks():
   tick.label.set_fontsize(fsize)
for tick in ax[ax_idx].yaxis.get_major_ticks():
    tick.label.set_fontsize(fsize)
if (ax_idx == 0):
    ax[ax_idx].set_ylabel("Ops/sec", fontsize=fsize)

ax[ax_idx].set_xlabel("Get ratio", fontsize=fsize)
#ax[ax_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
ax[ax_idx].set_title("Value size 2048 bytes", fontsize=fsize-2)
handles, labels = ax[ax_idx].get_legend_handles_labels()
labels = [versions_map[label] for label in labels]        

fig.set_size_inches(12, 4)
plot_dir = "./plots"
plot_file_path = "./plots/networking"
save_plot(plot_dir, plot_file_path, fig, lgd)
plt.close(fig)
plt.tight_layout()
#plt.show()


# OVERHEAD PLOT
number_of_bars = 2
bar_area_percentage = 0.8
w = float(bar_area_percentage / number_of_bars)
x_axis_spacing = np.linspace(-bar_area_percentage/2 + w/2, bar_area_percentage/2 - w/2, num=number_of_bars)

fig, ax = plt.subplots(1, 2)

fsize = 20

ax_idx = 0
x_labels = ['50%', '90%']
y_values_pmdk = [pmdk_50_512, pmdk_90_512]
y_values_anchor_no_encr = [anchor_no_encr_scone_50_512, anchor_no_encr_scone_90_512]
y_values_anchor_encr = [anchor_encr_scone_50_512, anchor_encr_scone_90_512]

#x_index = np.arange(0, len(y_values_pmdk), 1) + x_axis_spacing[0]
#rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values_pmdk], width = w, 
#                                    color = colour[0], hatch = hatch[0],
#                                    edgecolor = 'black', align='center', label="pmdk")

reference = [float(i) for i in y_values_pmdk] 
lib_values = [float(i) for i in y_values_anchor_no_encr]                                  
values_to_plot = [x/y for x, y in zip(reference, lib_values)]

max_y_idx = max(values_to_plot)

x_index = np.arange(0, len(y_values_anchor_no_encr), 1) + x_axis_spacing[0]
formatted_values = ["%.2f" % float(value) for value in values_to_plot]
print("anchor_no_encr_scone 512 50% overhead : " + str(formatted_values))
rect = ax[ax_idx].bar(x_index, values_to_plot, width = w, 
                                    color = colour[0], hatch = hatch[0],
                                    edgecolor = 'black', align='center', label="anchor_no_encr_scone")

lib_values = [float(i) for i in y_values_anchor_encr]                                  
values_to_plot = [x/y for x, y in zip(reference, lib_values)]
x_index = np.arange(0, len(y_values_anchor_encr), 1) + x_axis_spacing[1]
formatted_values = ["%.2f" % float(value) for value in values_to_plot]
print("anchor_encr_scone 512 50% overhead : " + str(formatted_values))
rect = ax[ax_idx].bar(x_index, values_to_plot, width = w, 
                                    color = colour[1], hatch = hatch[1],
                                    edgecolor = 'black', align='center', label="anchor_encr_scone")

ax[ax_idx].xaxis.set_ticks(range(0,len(x_labels)))
ax[ax_idx].xaxis.set_ticklabels(x_labels)
ax[ax_idx].ticklabel_format(axis='y', scilimits=[-3, 3])
ax[ax_idx].yaxis.get_offset_text().set_fontsize(fsize-3)

#plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
for tick in ax[ax_idx].xaxis.get_major_ticks():
   tick.label.set_fontsize(fsize)
for tick in ax[ax_idx].yaxis.get_major_ticks():
    tick.label.set_fontsize(fsize)
if (ax_idx == 0):
    ax[ax_idx].set_ylabel("Relative throughput overhead\nw.r.t. PMDK + eRPC w/o Enc", fontsize=fsize-4)

ax[ax_idx].set_xlabel("Get ratio", fontsize=fsize)
#ax[ax_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
ax[ax_idx].set_title("Value size 512 bytes", fontsize=fsize-2)
handles, labels = ax[ax_idx].get_legend_handles_labels()
labels = [versions_map[label] for label in labels]
print(labels)
if (ax_idx == 0): #to set the legend once and in the middle
    lgd = ax[ax_idx].legend(handles, labels, loc='upper center', #bbox_to_anchor=(0.5,-0.12),
                            bbox_to_anchor=(0.535, 1.22, 1., .102), #loc='lower left',
                            ncol=2, borderaxespad=0., fontsize=fsize-3, columnspacing=0.3)          

ax_idx = 1
x_labels = ['50%', '90%']
y_values_pmdk = [pmdk_50_2048, pmdk_90_2048]
y_values_anchor_no_encr = [anchor_no_encr_scone_50_2048, anchor_no_encr_scone_90_2048]
y_values_anchor_encr = [anchor_encr_scone_50_2048, anchor_encr_scone_90_2048]

#x_index = np.arange(0, len(y_values_pmdk), 1) + x_axis_spacing[0]
#rect = ax[ax_idx].bar(x_index, [float(i) for i in y_values_pmdk], width = w, 
#                                    color = colour[0], hatch = hatch[0],
#                                    edgecolor = 'black', align='center', label="pmdk")

reference = [float(i) for i in y_values_pmdk] 
lib_values = [float(i) for i in y_values_anchor_no_encr]                                  
values_to_plot = [x/y for x, y in zip(reference, lib_values)]

if (max(values_to_plot) > max_y_idx):
    max_y_idx = max(values_to_plot)

max_y_idx += 2
# print(max_y_idx)

x_index = np.arange(0, len(y_values_anchor_no_encr), 1) + x_axis_spacing[0]
formatted_values = ["%.2f" % float(value) for value in values_to_plot]
print("anchor_no_encr_scone 2048 50% overhead : " + str(formatted_values))
rect = ax[ax_idx].bar(x_index, values_to_plot, width = w, 
                                    color = colour[0], hatch = hatch[0],
                                    edgecolor = 'black', align='center', label="anchor_no_encr_scone")

lib_values = [float(i) for i in y_values_anchor_encr]                                  
values_to_plot = [x/y for x, y in zip(reference, lib_values)]
x_index = np.arange(0, len(y_values_anchor_encr), 1) + x_axis_spacing[1]
formatted_values = ["%.2f" % float(value) for value in values_to_plot]
print("anchor_encr_scone 2048 50% overhead : " + str(formatted_values))
rect = ax[ax_idx].bar(x_index, values_to_plot, width = w, 
                                    color = colour[1], hatch = hatch[1],
                                    edgecolor = 'black', align='center', label="anchor_encr_scone")

ax[ax_idx].xaxis.set_ticks(range(0,len(x_labels)))
ax[ax_idx].xaxis.set_ticklabels(x_labels)
ax[ax_idx].ticklabel_format(axis='y', scilimits=[-3, 3])
ax[ax_idx].yaxis.get_offset_text().set_fontsize(fsize-3)

#plt.xticks(range(0,standard_x_ticks), x_values[benchmark][variant]["pmdk"])
for tick in ax[ax_idx].xaxis.get_major_ticks():
   tick.label.set_fontsize(fsize)
for tick in ax[ax_idx].yaxis.get_major_ticks():
    tick.label.set_fontsize(fsize)
if (ax_idx == 0):
    ax[ax_idx].set_ylabel("Relative throughput overhead w.r.t. PMDK + eRPC w/o Enc", fontsize=fsize)

ax[ax_idx].set_xlabel("Get ratio", fontsize=fsize)
#ax[ax_idx].set_title(plot_title + " - " + ' '.join(map(str, experiment_params)), fontsize=10)
ax[ax_idx].set_title("Value size 2048 bytes", fontsize=fsize-2)
handles, labels = ax[ax_idx].get_legend_handles_labels()
labels = [versions_map[label] for label in labels]        


plt.ylim(0, max_y_idx)
#plt.suptitle("Hashmap", fontsize=12)
fig.set_size_inches(12, 3)
plot_dir = "./plots"
plot_file_path = "./plots/networking_overhead"
save_plot(plot_dir, plot_file_path, fig, lgd)
plt.close(fig)
plt.tight_layout()