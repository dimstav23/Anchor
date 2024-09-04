#!/usr/bin/env bash

set -x

private_key=$(cat /home/$(whoami)/.ssh/id_rsa)

flags=(
  -f scone-env
  -t anchor_sigmod:latest
  --build-arg ssh_key="${private_key}"
)

docker build "${flags[@]}" . $@
