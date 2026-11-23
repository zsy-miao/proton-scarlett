#!/usr/bin/env bash
# Repeat the RTX/NixOS launch test with a read-only game directory.
set -euo pipefail
umask 077
game=$(realpath "${1:?usage: test-launch-nixos.sh GAME-FOLDER [INSTALLED-TOOL]}")
tool=$(realpath "${2:-$HOME/.local/share/Steam/compatibilitytools.d/scarlett-proton}")
test -r "$game/game_scarlett_master.exe" && test -x "$tool/scarlett/xcl.exe" || exit 1
closure=$(nix-store -qR "$(realpath "$(command -v steam-run)")")
fhs=$(grep -E '/[^/]+-steam-run-[^/]+-fhsenv-rootfs$' <<<"$closure")
bwrap=$(grep -E '/[^/]+-bubblewrap-[0-9.]+$' <<<"$closure")/bin/bwrap
xvfb=$(realpath "$(command -v Xvfb)")
work=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-run.XXXXXXXX")
echo "Test output (including prefix): $work"
mkdir -p "$work"/{home,compat,steam,logs,runtime}
touch "$work/lock"
echo "tester:x:$(id -u):$(id -g)::/work/home:/bin/bash" >"$work/passwd"
gpu=()
for node in /dev/dri /dev/nvidia0 /dev/nvidiactl /dev/nvidia-modeset /dev/nvidia-uvm; do
    gpu+=(--dev-bind "$node" "$node")
done
exec timeout -k 5s 90s "$bwrap" --unshare-all --die-with-parent --new-session --clearenv \
    --ro-bind /nix/store /nix/store --ro-bind "$fhs/usr" /usr \
    --symlink usr/bin /bin --symlink usr/lib /lib --symlink usr/lib64 /lib64 --symlink usr/lib32 /lib32 \
    --proc /proc --dev /dev --tmpfs /dev/shm --tmpfs /tmp --ro-bind /sys /sys \
    --ro-bind /run/opengl-driver /run/opengl-driver "${gpu[@]}" --ro-bind "$work/passwd" /etc/passwd \
    --bind "$work" /work --ro-bind "$game" /game --ro-bind "$tool" /proton --bind "$work/lock" /proton/dist.lock \
    --setenv HOME /work/home --setenv USER tester --setenv LANG C --setenv PATH /usr/bin \
    --setenv DISPLAY :93 --setenv XDG_RUNTIME_DIR /work/runtime \
    --setenv LD_LIBRARY_PATH /run/opengl-driver/lib:/usr/lib64:/usr/lib32 \
    --setenv VK_DRIVER_FILES /run/opengl-driver/share/vulkan/icd.d/nvidia_icd.json \
    --setenv STEAM_COMPAT_CLIENT_INSTALL_PATH /work/steam --setenv STEAM_COMPAT_DATA_PATH /work/compat \
    --setenv SteamGameId 999001 --setenv SteamAppId 999001 \
    --setenv PROTON_LOG 1 --setenv PROTON_LOG_DIR /work/logs \
    --setenv PYTHONDONTWRITEBYTECODE 1 --chdir /game -- /bin/bash -ec '
        "$1" :93 -screen 0 800x600x24 -nolisten tcp >/work/logs/xvfb.log 2>&1 &
        for i in {1..50}; do [[ -S /tmp/.X11-unix/X93 ]] && break; sleep 0.1; done
        [[ -S /tmp/.X11-unix/X93 ]]
        exec /proton/proton waitforexitandrun /game/game_scarlett_master.exe
    ' _ "$xvfb" </dev/null >"$work/launcher.log" 2>&1
