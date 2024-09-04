with import <nixpkgs> {};
let
  localPkgs = callPackages ./nix {};
  kernel = linuxPackages_4_14.kernel;
in gcc8Stdenv.mkDerivation {
  name = "env";
  nativeBuildInputs = [
    bashInteractive
    clang
    autoconf
    automake
    cmake
    libtool
    pkg-config
  ];
  buildInputs = [
    python3Packages.numpy
    python3Packages.matplotlib
    python3Packages.oyaml
    python3Packages.pandas
    pandoc
    libndctl
    numactl
    snappy
    gflags
    rapidjson
    valgrind
    fuse
    tbb
    libuv
    ncurses
    glib
    libfabric
    gdb
    libunwind
    boost
    linuxHeaders
    gcc8
    protobuf
    vimPlugins.vim-clang-format
    clang-tools
    rdma-core
    gtest
    unzip
    tmate
    dmidecode
  ] ++ (with localPkgs; [ openssl gperftools ]);
  GPERF_PATH = localPkgs.gperftools;
  NIX_CFLAGS_COMPILE = ["-Wno-error" "-Wno-stringop-truncation" "-Wno-stringop-overflow" "-Wno-zero-length-bounds" "-Wno-uninitialized" "-Wno-maybe-uninitialized"];
  PMEM_IS_PMEM_FORCE = 1;
}
