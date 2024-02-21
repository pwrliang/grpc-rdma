import matplotlib.pyplot as plt
import re
import numpy as np
import os

import comm_settings


def read_phoenix_latency(size_list):
    median_lat = []
    p99_lat = []

    for size in size_list:
        path = "phoenix/rpc_bench_latency/rpc_bench_latency_{size}b/rpc_bench_client_10.22.81.19_1.stdout".format(
            size=size)
        with open(path, 'r') as fi:
            for line in fi:
                line = line.strip()
                m = re.search(r'median: (.*?)µs, p95: (.*?)µs, p99: (.*?)µs', line)
                if m is not None:
                    median_lat.append(float(m.groups()[0]))
                    p99_lat.append(float(m.groups()[1]))
    return median_lat, p99_lat


def rpc_aggregated_latency(polling_threads, lat_type):
    tp_list = []
    dir = 'varying-polling-threads'
    for n in polling_threads:
        path = "{dir}/client_polling_thread_{thread_num}_clients_640.log".format(
            dir=dir, thread_num=n)
        with open(path, 'r') as fi:
            for line in fi:
                match = re.match(r"RTT \(us\) mean (.*?), P50 (.*?), P95 (.*?), P99 (.*?), max (.*?)$", line)
                if match:
                    tp_list.append(float(match.groups()[lat_type]))
    return np.asarray(tp_list)


def compare_to_phoenix():
    size_list = (64, 128, 256, 512, 1024)  #

    fig, axs = plt.subplots(nrows=1, ncols=1, figsize=(4, 3.5))
    x = np.arange(len(size_list))  # the label locations
    lat_p50, lat_p99 = read_phoenix_latency(size_list)

    ax1 = axs

    ax1.plot(x, lat_p50, label="Median", )
    ax1.plot(x, lat_p99, label="P99", )

    ax1.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax1.set(xlabel='Number of Polling Threads', ylabel='Throughput (kRPCs/s)')
    ax1.xaxis.labelpad = 10
    # ax1.set_ylim((15, 20))
    ax1.set_xticks(x, size_list)
    # ax1.margins(x=0.01, )

    fig.tight_layout()
    fig.savefig('compare_to_phoenix.pdf', format='pdf', bbox_inches='tight')
    plt.show()


colors = ["gold", "red", "springgreen", "deepskyblue"]
markers = ['o', '^', '*', 's', ]
linestyle = ['-.', '-']


def varying_polling_threads():
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



    n_threads = (1, 2, 3, 4, 5, 6, 7, 8)  #

    fig, axs = plt.subplots(nrows=1, ncols=1, figsize=(4, 3.5))
    x = np.arange(len(n_threads))  # the label locations
    numa = "false"
    lat_p50 = rpc_aggregated_latency(n_threads, 1)
    lat_p99 = rpc_aggregated_latency(n_threads, 3)

    ax1 = axs

    ax1.plot(x, lat_p50, label="Median", color=colors[0], linestyle=linestyle[0], marker=markers[0])
    ax1.plot(x, lat_p99, label="P99", color=colors[1], linestyle=linestyle[1], marker=markers[1])

    print("Median", lat_p50)
    print("P99", lat_p99)

    ax1.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax1.set(xlabel='Number of Polling Threads', ylabel=r'Latency ($\mu$s)')
    ax1.xaxis.labelpad = 10
    ax1.set_ylim((10, 40))
    ax1.set_xticks(x, n_threads)
    # ax1.margins(x=0.01, )

    fig.tight_layout()
    fig.savefig('lat_polling_thread.pdf'.format(numa=numa), format='pdf', bbox_inches='tight')
    plt.show()


varying_polling_threads()
# compare_to_phoenix()
