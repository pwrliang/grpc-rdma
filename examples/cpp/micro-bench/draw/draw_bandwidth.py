import matplotlib.pyplot as plt
import re
import numpy as np
import os

import comm_settings


def varying_rb_size():
    rb_sizes = (32, 64, 128, 256, 512, 1024, 2048, 4096)  #

    def read_bandwidth(mode, req, rb_sizes):
        bd_list = []
        dir = 'varying-rb-size'
        for size in rb_sizes:
            path = "{dir}/client_bandwidth_{mode}_cli_1_req_{req}_ringbuf_{size}.log".format(
                dir=dir, mode=mode, req=req, size=size)
            with open(path, 'r') as fi:
                for line in fi:
                    match = re.search(r"Aggregated TX Bandwidth (.*?) Mb/s", line)
                    if match:
                        bd_list.append(float(match.groups()[0]))
        return np.asarray(bd_list)

    fig, axs = plt.subplots(nrows=1, ncols=1, figsize=(4.5, 3.5))
    x = np.arange(len(rb_sizes))  # the label locations
    bd_8 = read_bandwidth('RDMA_BP', 8192, rb_sizes)
    bd_32 = read_bandwidth('RDMA_BP', 32768, rb_sizes)
    bd_128 = read_bandwidth('RDMA_BP', 131072, rb_sizes)

    ax = axs
    colors = ["gold", "red", "springgreen", "deepskyblue"]
    patterns = ['xxx', '', '', '']

    dataset = []
    dataset.append((bd_8, '8KB Payload'))
    dataset.append((bd_32, '32KB Payload'))
    dataset.append((bd_128, '128KB Payload'))

    for bd, size in dataset:
        print(size, bd / 1024)

    idx = 1
    width = 0.2  # the width of the bars
    for data, label in dataset:
        if data is not None and len(data) > 0:
            data = data / 1024  # Gb/s
            bar = ax.bar((x + idx * width), data, width, label=label,
                         edgecolor='black', facecolor=colors[idx - 1], )
            idx += 1

    # ax1.set_xticks(x + width / 2. * idx , n_clients)
    ax.set_xticks(x + width / 3 * idx, rb_sizes)

    ax.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
              frameon=False)
    ax.set(xlabel='Ring Buffer Size (KB)', ylabel='Bandwidth (Gb/s)')
    # ax.xaxis.labelpad = 10
    ax.set_ylim((0, 50))
    ax.margins(x=0.02, )

    # fig.tight_layout()
    fig.savefig('varying_rb_size.pdf', format='pdf', bbox_inches='tight')
    # plt.show()


def zerocopy():
    payload_size = (65536, 131072, 262144)

    def read_bandwidth(mode, zerocopy=False):
        bd_list = []
        dir = 'zerocopy'
        for req in payload_size:
            if zerocopy:
                th = "32"
            else:
                th = "1048576"
            path = "{dir}/client_bandwidth_{mode}_cli_1_req_{req}_zerocopy_threshold_{th}.log".format(
                dir=dir, mode=mode, req=req, th=th)
            with open(path, 'r') as fi:
                for line in fi:
                    match = re.search(r"Aggregated TX Bandwidth (.*?) Mb/s", line)
                    if match:
                        bd_list.append(float(match.groups()[0]))
        return np.asarray(bd_list)

    fig, axs = plt.subplots(nrows=1, ncols=1, figsize=(4.5, 3.5))
    x = np.arange(len(payload_size))  # the label locations
    bd_copy = read_bandwidth('RDMA_BP', False)
    bd_zerocopy = read_bandwidth('RDMA_BP', True)

    ax = axs
    colors = ["gold", "red", "springgreen", "deepskyblue"]
    patterns = ['xxx', '', '', '']

    dataset = []
    dataset.append((bd_copy, 'Zerocopy Disabled'))
    dataset.append((bd_zerocopy, 'Zerocopy Enabled'))

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
    ax.set_xticks(x + width / 2 * idx, np.asarray(payload_size) // 1024)

    ax.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
              frameon=False)
    ax.set(xlabel='Payload Size (KB)', ylabel='Bandwidth (Gb/s)')
    # ax.xaxis.labelpad = 10
    ax.set_ylim((0, 100))
    ax.margins(x=0.1, )

    # fig.tight_layout()
    fig.savefig('zerocopy.pdf', format='pdf', bbox_inches='tight')
    # plt.show()


# zerocopy()
varying_rb_size()
