import matplotlib.pyplot as plt
import re
import numpy as np
import os

import comm_settings


def varying_polling_thread():
    def rpc_throughput(n_threads):
        tp_list = []
        dir = 'tput-varying-polling-threads'
        for n in n_threads:
            path = "{dir}/client_tput_polling_thread_{polling_thread}_clients_64.log".format(
                dir=dir, polling_thread=n)
            with open(path, 'r') as fi:
                for line in fi:
                    match = re.search(r"RPC Rate (.*?) RPCs/s", line)
                    if match:
                        tp_list.append(float(match.groups()[0]))
        return np.asarray(tp_list)

    def rpc_aggregated_latency(polling_threads, lat_type):
        lat_list = []
        dir = 'lat-varying-polling-threads'
        for n in polling_threads:
            path = "{dir}/client_lat_polling_thread_{thread_num}_clients_320.log".format(
                dir=dir, thread_num=n)
            with open(path, 'r') as fi:
                for line in fi:
                    match = re.match(r"RTT \(us\) mean (.*?), P50 (.*?), P95 (.*?), P99 (.*?), max (.*?)$", line)
                    if match:
                        lat_list.append(float(match.groups()[lat_type]))
        return np.asarray(lat_list)

    markers = ['o', '^', '*', 's', ]
    linestyle = ['-.', '-']
    n_threads = (1, 2, 3, 4, 5, 6, 7, 8)
    colors = ["gold", "red", "springgreen", "deepskyblue"]

    fig, axs = plt.subplots(nrows=1, ncols=2, figsize=(8.5, 3.5))
    x = np.arange(len(n_threads))  # the label locations

    ax1 = axs[0]

    lat_p50 = rpc_aggregated_latency(n_threads, 1)
    lat_p99 = rpc_aggregated_latency(n_threads, 3)

    ax1.plot(x, lat_p50, label="Median", color=colors[0], linestyle=linestyle[0], marker=markers[0])
    ax1.plot(x, lat_p99, label="P99", color=colors[1], linestyle=linestyle[1], marker=markers[1])
    ax1.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax1.set(xlabel='Number of Polling Threads', ylabel=r'Latency ($\mu$s)')
    # ax1.xaxis.labelpad = 10
    ax1.set_ylim((15, 40))
    ax1.set_xticks(x, n_threads)
    ax1.set_title("(a) Latency under Limited Request Rate")

    bpev_tp = rpc_throughput(n_threads)

    ax2 = axs[1]
    colors = ["gold", "red", "springgreen", "deepskyblue"]

    dataset = []
    dataset.append((bpev_tp, 'RR-Comp (BPEV)'))
    for data, label in dataset:
        if data is not None and len(data) > 0:
            data = data / 1000 / 1000  # kRPCs
            ax2.plot(x, data, label="Throughput", linestyle=linestyle[0], marker=markers[0], color=colors[1], )

    print(bpev_tp)

    ax2.set_xticks(x, n_threads)
    ax2.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax2.set(xlabel='Number of Polling Threads', ylabel='Throughput (MRPCs/s)')
    # ax2.xaxis.labelpad = 10
    ax2.set_ylim((0, 8))
    ax2.margins(x=0.05, )
    ax2.set_title("(b) Throughput without a Delay")

    # fig.tight_layout()
    fig.savefig('varying_polling_thread.pdf', format='pdf', bbox_inches='tight')
    # plt.show()


varying_polling_thread()
