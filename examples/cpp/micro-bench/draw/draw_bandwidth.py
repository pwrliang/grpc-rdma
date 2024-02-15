import matplotlib.pyplot as plt
import re
import numpy as np
import os

import comm_settings


def read_bandwidth(mode, req, rb_sizes):
    bd_list = []
    dir = 'varying-rb-size'
    for size in rb_sizes:
        path = "{dir}/client_bandwidth_{mode}_cli_4_req_{req}_ringbuf_{size}.log".format(
            dir=dir, mode=mode, req=req, size=size)
        with open(path, 'r') as fi:
            for line in fi:
                match = re.search(r"Aggregated TX Bandwidth (.*?) Mb/s", line)
                if match:
                    bd_list.append(float(match.groups()[0]))
    return np.asarray(bd_list)


rb_sizes = (16, 32, 64, 128, 256, 512, 1024, 2048)  #

fig, axs = plt.subplots(nrows=1, ncols=1, figsize=(4.5, 3.5))
x = np.arange(len(rb_sizes))  # the label locations
bd_8 = read_bandwidth('RDMA_BP', 8192, rb_sizes)
bd_32 = read_bandwidth('RDMA_BP', 32768, rb_sizes)
bd_128 = read_bandwidth('RDMA_BP', 131072, rb_sizes)
bd_512 = read_bandwidth('RDMA_BP', 524288, rb_sizes)

ax = axs
colors = ["gold", "red", "springgreen", "deepskyblue"]
patterns = ['xxx', '', '', '']

dataset = []
dataset.append((bd_8, '8KB'))
dataset.append((bd_32, '32KB'))
dataset.append((bd_128, '128KB'))

print(dataset)

idx = 1
width = 0.2  # the width of the bars
for data, label in dataset:
    if data is not None and len(data) > 0:
        data = data / 1000  # Gb/s
        bar = ax.bar((x + idx * width), data, width, label=label,
                     edgecolor='black', facecolor=colors[idx - 1], )
        idx += 1

# ax1.set_xticks(x + width / 2. * idx , n_clients)
ax.set_xticks(x + width / 3 * idx, rb_sizes)

ax.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
          frameon=False)
ax.set(xlabel='Ring Buffer Size (KB)', ylabel='Bandwidth (Gb/s)')
# ax.xaxis.labelpad = 10
ax.set_ylim((0, 100))
ax.margins(x=0.02, )

# fig.tight_layout()
fig.savefig('varying_rb_size.pdf', format='pdf', bbox_inches='tight')
# plt.show()
