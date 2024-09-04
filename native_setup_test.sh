#!/bin/sh

echo "Setting up native"
./setup.sh -e native -b benchmarks -a /home/$(whoami)/Anchor/ -p /home/$(whoami)/Anchor/pmdk/
echo "Running native"
./run_native.sh -e native -b benchmarks -a /home/$(whoami)/Anchor/ -p /home/$(whoami)/Anchor/pmdk/
rm -rf pmdk
git submodule update --init

echo "Setting up write_amplification"
./setup.sh -e native -b write_amplification -a /home/$(whoami)/Anchor/
echo "Running write_amplification"
./run_native.sh -e native -b write_amplification -a /home/$(whoami)/Anchor/
rm -rf pmdk
git submodule update --init

echo "Setting up breakdown"
./setup.sh -e native -b breakdown -a /home/$(whoami)/Anchor/ -p /home/$(whoami)/Anchor/pmdk/
echo "Running breakdown"
./run_native.sh -e native -b breakdown -a /home/$(whoami)/Anchor/ -p /home/$(whoami)/Anchor/pmdk/
rm -rf pmdk
git submodule update --init