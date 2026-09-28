#!/usr/bin/env bash
# Download and verify the pinned Hexagon cross toolchain into ~/hexagon.
#
# Shared by the hexagon-qemu and icount-ratchet CI jobs (.github/workflows/
# ci.yml sets the env below): both jobs write the same digest-keyed cache
# entry, so both must verify the same pin before anything lands under it —
# one script keeps the two writers from drifting apart.
#
#   HEXAGON_TOOLCHAIN_URL     release artifact (CodeLinaro)
#   HEXAGON_TOOLCHAIN_SHA256  hard pin; the download must match exactly
set -euo pipefail

: "${HEXAGON_TOOLCHAIN_URL:?set by the CI workflow}"
: "${HEXAGON_TOOLCHAIN_SHA256:?set by the CI workflow}"

mkdir -p ~/hexagon && cd ~/hexagon
curl -sfLo toolchain.tar.zst "$HEXAGON_TOOLCHAIN_URL"
actual=$(sha256sum toolchain.tar.zst | cut -d' ' -f1)
echo "toolchain sha256: $actual (pin this in HEXAGON_TOOLCHAIN_SHA256)"

# Integrity check against the published SHA256SUMS, plus the hard pin. The
# SUMS file catches corruption and cache poisoning; only the pin catches an
# origin compromise.
curl -sfLo SHA256SUMS "$(dirname "$HEXAGON_TOOLCHAIN_URL")/SHA256SUMS"
expected=$(grep "$(basename "$HEXAGON_TOOLCHAIN_URL")" SHA256SUMS | awk '{print $1}' | head -1)
if [ -z "$expected" ] || [ "$actual" != "$expected" ]; then
    echo "::error::toolchain does not match published SHA256SUMS"
    exit 1
fi
if [ "$actual" != "$HEXAGON_TOOLCHAIN_SHA256" ]; then
    echo "::error::toolchain checksum mismatch against pinned value"
    exit 1
fi
tar --zstd -xf toolchain.tar.zst
rm toolchain.tar.zst SHA256SUMS
