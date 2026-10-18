{
  rev,
  lib,
  pkgs,
}:
pkgs.pkgsi686Linux.stdenv.mkDerivation {
  pname = "navi";
  version = "${rev}";
  src = ../.;

  nativeBuildInputs = with pkgs; [
    pkg-config
    makeWrapper
  ];

  buildInputs = with pkgs.pkgsi686Linux; [
    openssl
    curl
  ];

  postPatch = ''
    substituteInPlace src/core/log.cpp \
      --replace-fail "notify-send" "${lib.getExe pkgs.libnotify}"
  '';

  buildPhase = ''
    make audit-libs
  '';

  installPhase = ''
    mkdir -p $out/
    cp bin/libnavi.so $out/
    cp bin/library-inject.so $out/
  '';

  meta = {
    description = "Steamclient Modification for Linux";
    homepage = "https://github.com/AceSLS/navi";
    license = lib.licenses.agpl3Only;
    platforms = lib.platforms.linux;
  };
}
