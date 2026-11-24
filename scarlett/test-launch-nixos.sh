#!/usr/bin/env bash
# Repeat the RTX/NixOS launch test with a read-only game directory.
set -euo pipefail
umask 077
game=$(realpath "${1:?usage: test-launch-nixos.sh GAME-FOLDER [INSTALLED-TOOL]}")
tool=$(realpath "${2:-$HOME/.local/share/Steam/compatibilitytools.d/scarlett-proton}")
test -r "$game/game_scarlett_master.exe" && test -f "$tool/scarlett/d3d12_xs.dll" || exit 1
closure=$(nix-store -qR "$(realpath "$(command -v steam-run)")")
fhs=$(grep -E '/[^/]+-steam-run-[^/]+-fhsenv-rootfs$' <<<"$closure")
bwrap=$(grep -E '/[^/]+-bubblewrap-[0-9.]+$' <<<"$closure")/bin/bwrap
xvfb=$(realpath "$(command -v Xvfb)")
xrandr=$(realpath "$(command -v xrandr)")
work=$(mktemp -d "${TMPDIR:-/tmp}/scarlett-run.XXXXXXXX")
echo "Test output (including prefix): $work"
mkdir -p "$work"/{home,compat,steam,logs,runtime}
touch "$work/lock"
echo "tester:x:$(id -u):$(id -g)::/work/home:/bin/bash" >"$work/passwd"
gpu=()
for node in /dev/dri /dev/nvidia0 /dev/nvidiactl /dev/nvidia-modeset /dev/nvidia-uvm; do
    gpu+=(--dev-bind "$node" "$node")
done
seconds=${SCARLETT_TEST_SECONDS:-90}
[[ $seconds =~ ^[1-9][0-9]*$ ]] || { echo "Invalid SCARLETT_TEST_SECONDS" >&2; exit 2; }
ffmpeg=
case ${SCARLETT_RECORD:-0} in
    1) ffmpeg=$(realpath "$(command -v ffmpeg)") ;;
    0) ;;
    *) echo "SCARLETT_RECORD must be 0 or 1" >&2; exit 2 ;;
esac
exec timeout -k 5s "${seconds}s" "$bwrap" --unshare-all --die-with-parent --new-session --clearenv \
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
        fb=()
        [[ -z $3 ]] || fb=(-fbdir /tmp)
        "$1" :93 -noreset -screen 0 800x600x24 -nolisten tcp "${fb[@]}" >/work/logs/xvfb.log 2>&1 &
        for i in {1..50}; do [[ -S /tmp/.X11-unix/X93 ]] && break; sleep 0.1; done
        [[ -S /tmp/.X11-unix/X93 ]]
        # The default Xvfb mode advertises 0 Hz, which breaks DXGI frame timing.
        "$2" --newmode 800x600_60 40 800 840 968 1056 600 601 605 628 +hsync +vsync
        "$2" --addmode screen 800x600_60
        "$2" --output screen --mode 800x600_60
        title=(/proton/proton waitforexitandrun /game/game_scarlett_master.exe)
        [[ -n $3 ]] || exec "${title[@]}"
        # Short, flushed fragments keep the MP4 playable if the sandbox times out.
        env -u LD_LIBRARY_PATH "$3" -nostdin -loglevel error -re -f image2 -loop 1 \
            -framerate 12 -c:v xwd -i /tmp/Xvfb_screen0 \
            -c:v libx264 -threads 2 -preset veryfast -tune zerolatency -crf 24 -pix_fmt yuv420p \
            -g 12 -movflags +frag_keyframe+empty_moov+default_base_moof -flush_packets 1 /work/display.mp4 \
            -vf fps=1 -pix_fmt rgb24 -threads 1 -update 1 -atomic_writing 1 /work/display.png >/work/logs/capture.log 2>&1 &
        recorder=$!
        for i in {1..50}; do [[ -s /work/display.png ]] && break; kill -0 "$recorder"; sleep 0.1; done
        test -s /work/display.png
        status=0
        "${title[@]}" || status=$?
        kill -TERM "$recorder"
        wait "$recorder" || [[ $? == 255 ]]
        test -s /work/display.mp4
        exit "$status"
    ' _ "$xvfb" "$xrandr" "$ffmpeg" </dev/null >"$work/launcher.log" 2>&1
