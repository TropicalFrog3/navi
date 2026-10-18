#!/bin/bash

set -e

SCRIPT_DIR="$(dirname "$(realpath "$0")")"
cd "$SCRIPT_DIR"

TIMESTAMP="$(date "+%Y%m%d%H%M%S")"
VERSION="$(cat res/version)"

ARCH_PKG_DIR="pkg/navi"
PKG_VER="$(grep "pkgver=.*" "$ARCH_PKG_DIR/PKGBUILD" | cut -d "=" -f 2)"
PKG_REL="$(grep "pkgrel=.*" "$ARCH_PKG_DIR/PKGBUILD" | cut -d "=" -f 2)"

RELEASE_DIR="releases/$TIMESTAMP"

make_release()
{
	NAME="$1"
	DIR="$RELEASE_DIR/$NAME"

	if [ ! -d "$DIR" ]; then
		mkdir -p "$DIR"
	fi

	sh docker/build.sh zips
	mv zips/* $DIR
	#zips are named navi VERSION
	rename "navi $VERSION" "navi-Any-$NAME" $DIR/*

	cd "$ARCH_PKG_DIR"
	makepkg -Ccf

	cd "$SCRIPT_DIR"
	mv pkg/navi/*.pkg.tar.zst "$DIR"
	#name for package is navi-pkgver-pkgrel-arch
	#We do not replace arch, for future proofing
	rename "navi-$PKG_VER-$PKG_REL" "navi-Arch-$NAME" $DIR/*

	mv $DIR/* "$RELEASE_DIR"
	rm -r "$DIR"
}

DEBUG=1 make_release "debug"
make_release "release"
