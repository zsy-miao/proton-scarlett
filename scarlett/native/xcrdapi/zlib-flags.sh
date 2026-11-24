# Source from the MinGW build/test scripts; Wine builds this copy with Z_SOLO.
zdir=${SCARLETT_ZLIB_SRC:-"$(dirname "${BASH_SOURCE[0]}")/../../../wine/libs/zlib"}
[[ -r "$zdir/inflate.c" ]] || { echo "Set SCARLETT_ZLIB_SRC to the matching Wine source tree's libs/zlib" >&2; exit 1; }
zflags=(-I"$zdir" -DZ_SOLO -DNO_GZIP -DFAR= "$zdir"/{adler32,inffast,inflate,inftrees,zutil}.c)
