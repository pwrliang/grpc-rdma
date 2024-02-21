import matplotlib.pyplot as plt
import re
import numpy as np
import os

import comm_settings





def draw_bp_timeout():
    def rpc_throughput(timeout_list):
        tp_list = []
        dir = 'varying-bp-timeout'
        for timeout in timeout_list:
            path = "{dir}/client_tput_RDMA_BPEV_cli_1_bp_timeout_{timeout}.log".format(
                dir=dir, timeout=timeout)
            with open(path, 'r') as fi:
                for line in fi:
                    match = re.search(r"RPC Rate (.*?) RPCs/s", line)
                    if match:
                        tp_list.append(float(match.groups()[0]))
        return np.asarray(tp_list)

    n_threads = (0, 2, 4, 6, 8, 10, 12, 14)  #

    fig, axs = plt.subplots(nrows=1, ncols=1, figsize=(4.5, 3.5))
    x = np.arange(len(n_threads))  # the label locations
    numa = "false"
    bpev_tp = rpc_throughput(n_threads)

    ax1 = axs
    colors = ["gold", "red", "springgreen", "deepskyblue"]
    markers = ['o', '^', '*', 's', ]
    linestyle = ['-.', '-']

    ax1.plot(x, bpev_tp / 1000, label="Throughput", color=colors[1], linestyle=linestyle[1], marker=markers[1])

    ax1.set_xticks(x, n_threads)

    ax1.legend(loc="upper left", ncol=1, labelspacing=0.2, columnspacing=0.21, handletextpad=0.3, fontsize='medium',
               frameon=False)
    ax1.set(xlabel='Busy-polling Timeout (us)', ylabel='Throughput (kRPCs/s)')
    # ax1.xaxis.labelpad = 10
    ax1.set_ylim((0, 400))
    ax1.margins(x=0.1, )

    # fig.tight_layout()
    fig.savefig('tput_bp_timeout.pdf'.format(numa=numa), format='pdf', bbox_inches='tight')
    # plt.show()


draw_bp_timeout()