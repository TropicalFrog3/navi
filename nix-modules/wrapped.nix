{
  pkgs,
  lib,
  rev,
  callPackage,
}:
let
  sls-steam = callPackage ./default.nix {
    inherit rev;
  };
in
pkgs.stdenv.mkDerivation {
  pname = "navi-wrapper";
  version = rev;

  src = sls-steam;

  nativeBuildInputs = [ pkgs.makeWrapper ];

  installPhase = ''
    mkdir -p $out/bin
    makeWrapper ${pkgs.steam}/bin/steam $out/bin/navi \
      --set LD_AUDIT "${sls-steam}/library-inject.so:${sls-steam}/libnavi.so"
  '';

  meta = {
    description = "Steamclient Modification for Linux";
    platforms = lib.platforms.linux;
    mainProgram = "navi";
  };
}
