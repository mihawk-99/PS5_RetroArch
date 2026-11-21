#!/usr/bin/env bash
# Rebuild one pinned SDK object; leave Lapy's kernel transaction/guards untouched.
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
sdk="$root/.deps/native/ps5-payload-sdk"
fork="${PS5_PAYLOAD_SDK_FORK:-$root/../PS5_PayloadSDK}"
revision=$(<"$sdk/.ps5-sdk-revision")
[[ $revision =~ ^[0-9a-f]{40}$ ]]
work="$root/build/sdk-elevation"
mkdir -p "$work"
exec 9>"$root/.deps/native/.ps5-sdk.lock"
flock 9
stamp=$(printf '%s\n' "$revision"; sha256sum "$root/platform/sdk/elevation-root-proof.patch" \
    "$root/tools/patch-sdk-elevation.sh" "$sdk/bin/prospero-clang")
stamp=$(printf '%s' "$stamp" | sha256sum | cut -d' ' -f1)
if [[ -f $sdk/.ps5-elevation-proof-sha256 && $(<"$sdk/.ps5-elevation-proof-sha256") == "$stamp" ]]; then
    exit 0
fi
mkdir -p "$work/platform/src"
git -C "$fork" show "$revision:platform/src/elevation.c" > "$work/platform/src/elevation.c"
patch --batch --fuzz=0 -p1 -d "$work" -i "$root/platform/sdk/elevation-root-proof.patch"
export PS5_CLANG=${PS5_CLANG:-/usr/bin/clang}
"$sdk/bin/prospero-clang" -std=c11 -O2 -Wall -Wextra -Werror -fPIC -femulated-tls \
    -c "$work/platform/src/elevation.c" -o "$work/elevation.o"
[[ $("$sdk/bin/prospero-ar" t "$sdk/target/lib/libps5platform.a" | grep -cx elevation.o) == 1 ]]
cp -- "$sdk/target/lib/libps5platform.a" "$work/libps5platform.a"
"$sdk/bin/prospero-ar" rcs "$work/libps5platform.a" "$work/elevation.o"
mv -- "$work/libps5platform.a" "$sdk/target/lib/libps5platform.a"
printf '%s\n' "$stamp" > "$sdk/.ps5-elevation-proof-sha256"
echo '==> [sdk] elevation client waits for root before opening its filesystem proof' >&2
