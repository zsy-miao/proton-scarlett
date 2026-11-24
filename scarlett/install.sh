#!/usr/bin/env bash
# Install an independent Steam compatibility tool using the matching Valve runtime.
set -euo pipefail
if (( $# != 3 )); then
    echo "usage: $0 'path/to/Proton 11.0' built-runtime-directory path/to/compatibilitytools.d/scarlett-proton" >&2
    exit 2
fi
src=$(realpath "$(dirname "$0")/..")
base=$(realpath "$1")
runtime=$(realpath "$2")
dest=$(realpath -m "$3")
test -f "$base/files/bin/wine" && test -f "$base/toolmanifest.vdf" || exit 1
# Do not mix a launcher with a depot whose prefix format or directory layout changed.
printf '%s  %s\n' a4f94c208355639c87877ba63e8faf1f26d8c0731db04f722dee66cdfcfbf478 "$base/proton" | sha256sum --check --status ||
    { echo "The Valve runtime does not match this fork (expected Proton 11.0-2e)." >&2; exit 1; }
[[ ! -e $dest && ! -L $dest && $dest != "$base/"* ]] || { echo "Choose a new destination outside the Valve depot." >&2; exit 1; }
mkdir -p "$(dirname "$dest")"
stage=$(mktemp -d "$(dirname "$dest")/.scarlett.XXXXXXXX")
trap 'rm -rf -- "$stage"' EXIT
cp -a --reflink=auto "$base/." "$stage/"
install -m755 "$src/proton" "$stage/proton"
if [[ -f "$runtime/xaudio2_9.dll" ]]; then
    install -m644 "$runtime/xaudio2_9.dll" "$stage/files/lib/wine/x86_64-windows/xaudio2_9.dll"
fi
mkdir "$stage/scarlett"
install -m644 "$runtime"/{d3d12_xs,xmem,chad,xapu,AcpHal,xg_xs,XCRDAPI}.dll "$stage/scarlett/"
sed -e 's/##INTERNAL_TOOL_NAME##/scarlett-proton/g' -e 's/##BUILD_NAME##/scarlett-proton/g' \
    "$src/compatibilitytool.vdf.template" >"$stage/compatibilitytool.vdf"
mv -Tn -- "$stage" "$dest"
[[ ! -d $stage ]] || { echo "Destination appeared during installation." >&2; exit 1; }
echo "Installed $dest"
