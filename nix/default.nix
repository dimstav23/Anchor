{ pkgs ? (import <nixpkgs> {}) }: rec {
  openssl = pkgs.callPackage ./openssl.nix {};
  gperftools = pkgs.callPackage ./gperftools.nix {};
  pmdk = pkgs.callPackage ./pmdk.nix {};
  libpmemobj-cpp = pkgs.callPackage ./libpmemobj-cpp.nix {};
  #  inherit pmdk;
  #};
  memkind = pkgs.callPackage ./memkind.nix {
  	inherit libpmemobj-cpp;
  };
  onetbb = pkgs.callPackage ./tbb.nix {};
}
