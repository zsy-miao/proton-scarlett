#!/usr/bin/env bash
# Compile the provider's actual submit-time checks against a small CPU registry.
set -euo pipefail
cd "$(dirname "$0")"
w=$(mktemp -d)
trap 'rm -rf "$w"' EXIT
sed -n '/^static struct color_resource \*sr_image(/,/^}/p; /^static BOOL sr_descriptor(/,/^}/p; /^BOOL xs_sr_submitted(/,/^}/p; /^static BOOL sr_live(/,/^}/p; /^HRESULT xs_reuse_with_sr(/,/^}/p; /^HRESULT xs_reuse_common(/,+1p' ../native/d3d12_device.c >"$w/reuse.inc"
cc -std=c11 -O2 -Wall -Wextra -Werror -I"$w" restore-ticket.c -o "$w/check"
"$w/check"
