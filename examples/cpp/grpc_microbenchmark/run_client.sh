mpirun --hostfile hostfile --bind-to none --mca btl_openib_allow_ib 1 \
  ./cmake-build-debug-mri/greeter_async_client2 -mode RDMA_BP -host mi100-01 -req 100 -batch 100

  #-mca btl_tcp_if_include ib0 --mca btl_openib_allow_ib 1 \