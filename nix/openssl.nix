{ stdenv, fetchFromGitHub, perl, pkgs ? (import <nixpkgs> {}) } :
stdenv.mkDerivation rec {
  pname = "openssl";
  version = "1_1_1_stable";

  src = fetchFromGitHub {
    owner = "dimstav23";
    repo = "openssl";
    rev = "ad8e83cf11187388c71cfbdb70880d9e7ed26e0e";
    sha256 = "UwnCUZCm4o25X8/8nAjRRNUysS9SPPWtj2Bv18Y5wjE=";
  };
  enableParallelBuilding = true;
  nativeBuildInputs = [ perl ];
  
  configurePhase = ''
    mkdir -p $out/{bin,include,lib,share}
    substituteInPlace ./config --replace '/usr/bin/env' '${pkgs.coreutils}/bin/env'
    ./config --prefix=$out --openssldir=$out
  '';
    
  buildPhase = ''
    make
  '';
    
  installPhase = ''
    make install
  '';
}

