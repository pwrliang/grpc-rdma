import matplotlib.pyplot as plt
import re
import numpy as np
import os

import comm_settings

colors = ["gold", "red", "springgreen", "deepskyblue"]
markers = ['o', '^', '*', 's', ]
linestyle = ['-.', '-']


def rr_vs_mrpc():
    def read_resources(mode, n_clients):
        cpu_usr_p50 = []
        cpu_sys_p50 = []
        mem_p50 = []
        cpu_usr_p99 = []
        cpu_sys_p99 = []
        mem_p99 = []

        dir = 'stat-rate-limit'
        for n in n_clients:
            path = "{dir}/server_stat_lat_{mode}_cli_{n_clients}_concurrent_1_streaming_true.log".format(
                dir=dir, mode=mode, n_clients=n)
            cpu_usr_list = []
            cpu_sys_list = []
            mem_list = []
            with open(path, 'r') as fi:
                for line in fi:
                    line = line.strip()
                    if line.startswith('#'):
                        continue
                    arr = line.split()
                    cpu_usr_list.append(float(arr[3]))
                    cpu_sys_list.append(float(arr[4]))
                    mem_list.append(float(arr[11]))

            cpu_usr_p50.append(np.percentile(np.asarray(cpu_usr_list), 50))
            cpu_sys_p50.append(np.percentile(np.asarray(cpu_sys_list), 50))
            mem_p50.append(np.percentile(np.asarray(mem_list), 50))

            cpu_usr_p99.append(np.percentile(np.asarray(cpu_usr_list), 99))
            cpu_sys_p99.append(np.percentile(np.asarray(cpu_sys_list), 99))
            mem_p99.append(np.percentile(np.asarray(mem_list), 99))
        return (np.asarray(cpu_usr_p50), np.asarray(cpu_sys_p50), np.asarray(mem_p50),
                np.asarray(cpu_usr_p99), np.asarray(cpu_sys_p99), np.asarray(mem_p99))

    n_clients = (1, 2, 4, 8, 16, 32, 64, 128)  #

    fig, axs = plt.subplots(nrows=1, ncols=2, figsize=(7.5, 3.5))
    x = np.arange(len(n_clients))  # the label locations
    rr_cpu_usr_p50, rr_cpu_sys_p50, rr_mem_p50, rr_cpu_usr_p99, rr_cpu_sys_p99, rr_mem_p99 = read_resources('RDMA_BPEV',
                                                                                                            n_clients)
    tcp_cpu_usr_p50, tcp_cpu_sys_p50, tcp_mem_p50, tcp_cpu_usr_p99, tcp_cpu_sys_p99, tcp_mem_p99 = read_resources('TCP',
                                                                                                                  n_clients)

    ax1 = axs[0]
    colors = ["gold", "red", "springgreen", "deepskyblue"]
    patterns = ['xxx', '', '', '']

    ax1.plot(x, tcp_cpu_usr_p50 + tcp_cpu_sys_p50, label="gRPC", color=colors[0], linestyle=linestyle[0],
             marker=markers[0])
    ax1.plot(x, rr_cpu_usr_p50 + rr_cpu_sys_p50, label="RR-Comp", color=colors[1], linestyle=linestyle[1],
             marker=markers[1])

    print(tcp_cpu_usr_p50 + tcp_cpu_sys_p50)
    print((rr_cpu_usr_p50 + rr_cpu_sys_p50) / (tcp_cpu_usr_p50 + tcp_cpu_sys_p50))

    ax1.set_xticks(x, n_clients)

    ax1.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax1.set(xlabel='Number of Clients', ylabel='CPU Usage (%)')
    # ax1.xaxis.labelpad = 10
    ax1.set_ylim((0, 4000))
    ax1.margins(x=0.05, y=0.4)
    ax1.set_title("(a) Median CPU Usage")

    ax2 = axs[1]

    ax2.plot(x, tcp_mem_p50 / 1024, label="gRPC", color=colors[0], linestyle=linestyle[0], marker=markers[0])
    ax2.plot(x, rr_mem_p50 / 1024, label="RR-Comp", color=colors[1], linestyle=linestyle[1], marker=markers[1])
    ax2.set_xticks(x, n_clients)

    ax2.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax2.set(xlabel='Number of Clients', ylabel='Memory Usage (MB)')
    # # ax1.xaxis.labelpad = 10
    # ax2.set_ylim((0, 35))
    # ax2.margins(x=0.05, )
    ax2.set_title("(b) Median Memory Usage")
    #
    fig.tight_layout()
    # fig.savefig('resources_no_limit.pdf', format='pdf', bbox_inches='tight')
    plt.show()


rr_vs_mrpc()
