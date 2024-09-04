{ stdenv, fetchFromGitHub, libpmemobj-cpp, perl, autoreconfHook, autoconf, automake, libtool, unzip, numactl, jemalloc}:
stdenv.mkDerivation rec {
  pname = "memkind";
  version = "v1.10.1";

  src = fetchFromGitHub {
    owner = "memkind";
    repo = "memkind";
    rev = version;
    sha256 = "knqSmg9HX3YND/2s8eGaDy31OSGm/hXcv+Wy4Q5CP4Y=";
  };

  enableParallelBuilding = true;
  nativeBuildInputs = [  
    perl
    autoconf
    automake
    libtool
    unzip
    autoreconfHook
    numactl
    jemalloc
  ];

  buildInputs = [ libpmemobj-cpp ];

  configurePhase = ''
    mkdir -p $out/{bin,include,lib,share}
    ./configure --prefix=$out
  '';
    
  buildPhase = ''
    make
  '';
    
  installPhase = ''
    make install
  '';

}
