#!/usr/bin/env bash
# From a Linux Wine build environment with MinGW x86_64 GCC, make, autoconf,
# flex, bison, m4, perl, python3, curl, git, and a native C compiler.
set -euo pipefail
patch=$(realpath "$(dirname "$0")/proton11-faudio.patch")
mkdir "${1:?usage: rebuild-xaudio.sh NEW-SCRATCH-DIR}"
cd "$1"
root=$PWD
mkdir wine build
curl -fsSLo wine.tar.gz https://codeload.github.com/ValveSoftware/wine/tar.gz/e60892cc2398b0a83c332a2078c7b85e47bdb42b
for xml in vk video; do
  curl -fsSLo "$xml.xml" "https://raw.githubusercontent.com/KhronosGroup/Vulkan-Docs/v1.4.339/xml/$xml.xml"
done
sha256sum -c <<'SUMS'
d61805d35fb3da26fe3e0f77d68268eb81ceb97cfda326d2d58725be43897416  wine.tar.gz
5ebddf02358d937d8ce9bd997b20d2be9693e48320e1a060056c26b9cfe15438  vk.xml
4480ab87c8ee6d561376fa83820af1071f85ff7fa1a26b21f521717fa04d5809  video.xml
SUMS
echo "c8fd287720dcf20e5e2714212bcb5725206d06c5713e2e167f5f5b8350db19cc  $patch" | sha256sum -c -
tar -xzf wine.tar.gz --strip-components=1 -C wine
cd wine
git apply --check "$patch"
git apply "$patch"
perl tools/make_requests
perl tools/make_specfiles
XDG_CACHE_HOME="$root/cache" python3 dlls/winevulkan/make_vulkan -x "$root/vk.xml" -X "$root/video.xml"
autoreconf -ifv
cd ../build
bash ../wine/configure --enable-win64 --without-x --without-wayland --disable-tests --without-freetype
target=dlls/xaudio2_9/x86_64-windows/xaudio2_9.dll
make -j"${JOBS:-8}" "$target"
test -s "$target"
sha256sum "$PWD/$target"
