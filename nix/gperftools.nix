{ stdenv, fetchFromGitHub, autoreconfHook, libunwind } :
stdenv.mkDerivation rec {
  pname = "gperftools";
  version = "resume_pause";

  src = fetchFromGitHub {
    owner = "dimstav23";
    repo = "gperftools";
    rev = "890a74bc94a6c0e4594396540f853fdd528ef2fc";
    sha256 = "URLPEhCtuDlxFt1U5+KNPDyucfC0ph09K7n6JvjMSUw=";
  };
  enableParallelBuilding = true;
  nativeBuildInputs = [ autoreconfHook ];
  buildInputs = [ libunwind ];
  
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

