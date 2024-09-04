# Anchor network artifact documentation

## Compile native network stack
To compile the native network stack:
```
$ cd /path/to/Anchor/network-stack
$ git submodule update --init
$ cd /path/to/Anchor/network-stack/client_server_twosided/dpdk
# you can optionally remove kernel and app from GNUMakefile for simplicity
$ make install T=x86_64-native-linuxapp-gcc DESTDIR=../dpdk_static -j$(nproc)
$ cd /path/to/Anchor/network-stack/client_server_twosided/eRPC4dpdk
$ cmake . -DTRANSPORT=dpdk -DPERF=off -DLOG_LEVEL=none
$ make -j $(nproc)
$ cd /path/to/Anchor/network-stack/client_server_twosided
$ mkdir -p build && cd build
$ cmake .. -DTRANSPORT=dpdk -DMEASURE_LATENCY=off -DMEASURE_THROUGHPUT=on -DENCRYPT=on
$ git apply /path/to/Anchor/anchor_network/network-stack.patch
$ make -j $(nproc)
$ cd /path/to/Anchor/anchor_network
$ make clean && make
```
The `-DENCRYPT` option defines whether encryption is enabled or not.

## Compile native network stack
To compile the network stack inside SCONE:
```
$ cd /path/to/Anchor/anchor-eRPC/eRPC/build
$ make -f Makefile_dpdk_scone
$ rm CMakeCache.txt
$ cmake .. -DPERF=OFF -DTRANSPORT=dpdk -DSCONE=true -DLOG_LEVEL=none
$ make -j$(nproc)
$ cd /path/to/Anchor/network_stack/client_server_twosided
$ mkdir -p build && cd build
$ cp ../CMakeLists.txt ../CMakeLists_native.txt
$ cp ../CMakeLists_scone.txt ../CMakeLists.txt
$ cmake .. -DTRANSPORT=dpdk -DDIMITRA_SCONE=1 -DMEASURE_LATENCY=off -DMEASURE_THROUGHPUT=on -DENCRYPT=on
$ git apply /path/to/Anchor/anchor_network/network-stack.patch
$ make -j $(nproc)
$ cd /path/to/Anchor/anchor_network
$ make clean && make SCONE=1
```
The `-DENCRYPT` option defines whether encryption is enabled or not.

## Compile native network stack for PMDK
To compile the native network stack for PMDK:
```
$ cd /path/to/Anchor/network-stack
$ git submodule update --init
$ cd /path/to/Anchor/client_server_twosided/dpdk
# you can optionally remove kernel and app from GNUMakefile for simplicity
$ make install T=x86_64-native-linuxapp-gcc DESTDIR=../dpdk_static -j$(nproc)
$ cd /path/to/Anchor/network-stack/client_server_twosided
$ mkdir -p build && cd build
$ cmake .. -DTRANSPORT=dpdk -DMEASURE_LATENCY=off -DMEASURE_THROUGHPUT=on -DENCRYPT=off
$ git apply /path/to/Anchor/anchor_network/network-stack.patch
$ make -j $(nproc)
$ cd /path/to/Anchor/pmdk/pmdk_network
$ make clean && make
```

## Configuration for networking experiments:
To make sure that you configure the network interfaces correctly navigate to the dpdk usertools directory: \
`cd /path/to/Anchor/network-stack/client_server_twosided/dpdk/usertools` \
Run `python dpdk-devbind.py --status` to check that a network device uses a DPDK-compatible driver. \
This will list you the available interfaces.
If none of them uses a DPDK-compatible driver, run:
```
$ sudo python dpdk-devbind.py --bind=igb_uio [interface_name]
```
If all interfaces are active:
```
$ sudo ifconfig [interface_name] down
$ sudo python dpdk-devbind.py --bind=igb_uio [interface_name]
```

## Experiments execution
Example execution using the following server at UoE: \
**amy**: 10.243.29.181 *(server)* \
**donna**: 10.243.29.180 *(client)*

### For the native PMDK w/o encryption version:
Navigate to the `pmdk_network` folder:
```
$ cd /path/to/Anchor/pmdk/pmdk_network
```

**For the 50%-50% workloads**: \
server process:
```
$ sudo -E LD_LIBRARY_PATH=../src/nondebug/ ./server hashmap_tx /dev/shm/testfile $server_ip 50 `$datasize` 10
```
client process: 
```
$ sudo ./client $client_ip $server_ip hashmap_tx 50 `$datasize` > ../../results/pmdk/net_hashmap_tx_50_512_result_$i
```
where `$i` is the experiment repeat

**For the 90%-10% workloads**: \
server process:
```
$ sudo -E LD_LIBRARY_PATH=../src/nondebug/ ./server hashmap_tx /dev/shm/testfile $server_ip 90 `$datasize` 10
```
client process:
```
$ sudo ./client $client_ip $server_ip hashmap_tx 90 `$datasize` > ../../results/pmdk/net_hashmap_tx_90_`$datasize`_result_$i
```
where \
`$i`: the experiment repeat \
`$datasize`: the value size \
`$server_ip` : the IP of the server (**amy**) \
`$client_ip` : the IP of the client (**donna**)

### For the Scone Anchor version (w/ and w/o encryption):
**For the server process preparation**: \
Compile network stack in **SCONE** as described above (by appropriately setting the -DENCRYPT option to enable or disable the encryption) in the **SCONE** container.
Inside the **SCONE** container run the following to install the compiled libraries:
```
$ cp /usr/local/lib/libcrypto* /usr/lib/x86_64-linux-gnu/ 2>/dev/null \
$ cp /usr/lib/x86_64-linux-gnu/anchor_no_encr_scone/lib/* /usr/lib/x86_64-linux-gnu/ 2>/dev/null \
$ cp /home/Anchor/network-stack/client_server_twosided/build/libanchorclient.so /usr/lib/x86_64-linux-gnu/ 2>/dev/null \
$ cp /home/Anchor/network-stack/client_server_twosided/build/libanchorserver.so /usr/lib/x86_64-linux-gnu/ 2>/dev/null 
```
**For the client process preparation**: \
Compile the network stack natively (by appropriately setting the -DENCRYPT option to enable or disable the encryption).

Now that you've made the appropriate setup:
**For the 50%-50% workloads**: \
server process (inside the **SCONE** container):
```
$ Hugepagesize=2097152 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/7/ SCONE_VERSION=1 SCONE_LOG=0 SCONE_QUEUES=8 SCONE_NO_FS_SHIELD=1 SCONE_NO_MMAP_ACCESS=1 SCONE_HEAP=2G  SCONE_LD_DEBUG=1 SCONE_MODE=hw /opt/scone/lib/ld-scone-x86_64.so.1 ./server hashmap_tx /dev/shm/testfile $server_ip 50 $datasize 10
```
client process (natively):
```
$ sudo touch ../scone/results/$result_folder/net_hashmap_tx_50_$datasize_result_$i && sudo ./client $client_ip $server_ip hashmap_tx 50 $datasize | sudo tee ../scone/results/$result_folder/net_hashmap_tx_50_$datasize_result_$i
```
**For the 90%-10% workloads**: \
server process (inside the **SCONE** container): 
```
$ Hugepagesize=2097152 LD_LIBRARY_PATH=/usr/lib/x86_64-linux-gnu/:/usr/lib/gcc/x86_64-linux-gnu/7/ SCONE_VERSION=1 SCONE_LOG=0 SCONE_QUEUES=8 SCONE_NO_FS_SHIELD=1 SCONE_NO_MMAP_ACCESS=1 SCONE_HEAP=2G  SCONE_LD_DEBUG=1 SCONE_MODE=hw /opt/scone/lib/ld-scone-x86_64.so.1 ./server hashmap_tx /dev/shm/testfile $server_ip 90 $datasize 10
```
client process (natively):
```
$ sudo touch ../scone/results/$result_folder/net_hashmap_tx_90_$datasize_result_$i && sudo ./client $client_ip $server_ip hashmap_tx 90 $datasize | sudo tee ../scone/results/$result_folder/net_hashmap_tx_90_$datasize_result_$i
```
where \
`$i` : the experiment repeat \
`$datasize` : the value size \
`$result_folder` : the folder for the results (`anchor_encr_scone` or `anchor_no_encr_scone` for the Scone Anchor version w/ and w/o encryption respectively) \
`$server_ip` : the IP of the server (**amy**) \
`$client_ip` : the IP of the client (**donna**)
