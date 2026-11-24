#!/usr/bin/env bash
# Only the synthetic client is mounted. Do not inherit a desktop display.
set -euo pipefail
umask 077
here=$(realpath "$(dirname "$0")")
sdk=$(realpath "${1:?usage: test.sh PATH/TO/SDK-v0/GameInput.h [STOCK-PROTON-11-DIR]}")
proton=$(realpath "${2:-$HOME/.local/share/Steam/steamapps/common/Proton 11.0}")
work=$(mktemp -d "${TMPDIR:-/tmp}/gameinput.XXXXXXXX")
echo "GameInput fixture: $work"
mkdir -p "$work"/{test,include,home}
ln -s "$sdk" "$work/include/GameInput.h"
cd "$work/test"
flags=(-O2 -Wall -Wextra -Werror -Wno-unknown-pragmas -I"$work/include")
x86_64-w64-mingw32-g++ -std=c++20 "${flags[@]}" -shared "$here/gameinput.cpp" "$here/gameinput.def" \
    -luuid -static-libgcc -static-libstdc++ -o GameInput.dll
printf 'LIBRARY GameInput.dll\nEXPORTS\nGameInputCreate @1 NONAME\n' >import.def
x86_64-w64-mingw32-dlltool -d import.def -l import.a
x86_64-w64-mingw32-gcc "${flags[@]}" "$here/test.c" import.a -o test.exe
cp "$here/../../tests/private-xtest.py" .
touch "$work/lock"
closure=$(nix-store -qR "$(realpath "$(command -v steam-run)")")
root=$(grep -E '/[^/]+-steam-run-[^/]+-fhsenv-rootfs$' <<<"$closure")
bwrap=$(grep -E '/[^/]+-bubblewrap-[0-9.]+$' <<<"$closure")/bin/bwrap
timeout -k 3s 60s "$bwrap" --unshare-all --die-with-parent --new-session --clearenv \
    --ro-bind /nix/store /nix/store --ro-bind "$root/usr" /usr --symlink usr/bin /bin \
    --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/lib32 /lib32 \
    --dev /dev --proc /proc --tmpfs /tmp --ro-bind "$proton" /proton \
    --bind "$work/home" /home/test --ro-bind "$work/lock" /work/lock --ro-bind "$work/test" /test \
    --setenv LD_LIBRARY_PATH /usr/lib64:/usr/lib32 \
    --setenv PATH /usr/bin --setenv USER test --setenv HOME /home/test --setenv DISPLAY :93 \
    --setenv WINEPREFIX /home/test/pfx --setenv WINEDEBUG +loaddll --setenv WINEDLLOVERRIDES gameinput=n \
    --chdir /test -- /bin/bash -ec '
        "$1" :93 -noreset -screen 0 800x600x24 -nolisten tcp > /tmp/xvfb.log 2>&1 &
        for i in {1..50}; do [[ -S /tmp/.X11-unix/X93 ]] && break; sleep 0.1; done
        [[ -S /tmp/.X11-unix/X93 ]]
        exec /proton/files/bin/wine /test/test.exe
    ' _ "$(realpath "$(command -v Xvfb)")" >"$work/test.log" 2>&1
grep 'XTEST private Xvfb' "$work/test.log"
grep 'PASS ordinal 1:' "$work/test.log"
