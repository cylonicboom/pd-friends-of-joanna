#!/bin/sh
# bundle.sh — turn a run's artifacts into release files.
#
#   sh tools/ci/bundle.sh <tag> <artifacts-dir> <out-dir>
#
# <artifacts-dir> is what actions/download-artifact or `gh run download`
# leaves behind: one folder per artifact, named foj-<arch>-<os>, each holding
# the binary beside data/ (the mod, from basedir/data at build time). Each
# becomes <out-dir>/foj-<tag>-<arch>-<os>.<zip|tar.gz|tar.xz> with a top-level
# folder of the same name, plus one checksums file. The flatpak, if the run
# built one, is carried across as it is.
#
# Both the rolling ci-dev-build publish and a promoted release go through
# this, so what a tester downloads from either is the same file.
set -eu

tag=$1
src=$(cd "$2" && pwd)
out=$3
mkdir -p "$out"
out=$(cd "$out" && pwd)

for dir in "$src"/foj-*; do
	[ -d "$dir" ] || continue
	name=$(basename "$dir")                 # foj-x86_64-linux
	rel="foj-$tag-${name#foj-}"             # foj-v0.3.9.0-x86_64-linux
	rm -rf "$src/$rel"
	cp -R "$dir" "$src/$rel"
	case "$name" in
		*-windows|*-nswitch) (cd "$src" && zip -qr "$out/$rel.zip" "$rel") ;;
		*-osx)               tar cJf "$out/$rel.tar.xz" -C "$src" "$rel" ;;
		*)                   tar czf "$out/$rel.tar.gz" -C "$src" "$rel" ;;
	esac
	rm -rf "$src/$rel"
	echo "bundled $rel"
done

for fp in "$src"/*/*.flatpak; do
	[ -f "$fp" ] && cp "$fp" "$out/" && echo "carried $(basename "$fp")"
done

(cd "$out" && sha256sum foj-$tag-* > "foj-$tag-checksums.sha256")
ls -l "$out"
