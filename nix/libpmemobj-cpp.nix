{ stdenv, fetchFromGitHub, pmdk, perl, cmake }:
stdenv.mkDerivation rec {
  pname = "libpmemobj-cpp";
  version = "1.11";

  src = fetchFromGitHub {
    owner = "pmem";
    repo = "libpmemobj-cpp";
    rev = version;
    sha256 = "sha256-+wcSBv1gOQ1KDGsLsjUqxCRySdHRJJZLgsJnIOESwqk=";
  };
  enableParallelBuilding = true;
  buildInputs = [ pmdk ];
  nativeBuildInputs = [ cmake perl ];
}
