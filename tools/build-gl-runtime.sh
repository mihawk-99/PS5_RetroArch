#!/usr/bin/env bash
# PS5 RetroArch - namespace the ps5-opengl runtime so it can share a link with
# the Vulkan driver.
#
#   tools/build-gl-runtime.sh            build build/gl-archives/*.a, print paths
#
# The title links two GPU stacks grown from the same Mesa sources. RADV (the
# Vulkan driver) carries its own NIR/ACO/compiler objects, and the ps5-opengl
# SDK's archives carry theirs: about fifteen thousand symbols - nir_*, ac_*,
# util_*, blake3_*, the Mesa utility layer - are defined on both sides. Linked
# plainly the link fails on duplicate definitions; forced through, each stack's
# references would bind to whichever copy the linker saw first, handing the GL
# frontend RADV's pinned compiler internals or the reverse.
#
# The fix is namespacing, applied to the GL set only because it is the new
# member: every symbol that collides is redefined to ps5gl_<name> inside the GL
# archives with llvm-objcopy --redefine-syms, which renames definitions and
# references in each member alike, so the GL objects still bind to one another
# and the Vulkan objects are untouched. The public surface - gl*, egl* and the
# ps5_opengl_* entries - never collides with a Vulkan symbol and keeps its
# name.
#
# Output: build/gl-archives/<name>.a, one per member of the SDK's Core33 GROUP
# script, in GROUP order, printed one per line for the caller's link inputs.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

sdk="${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}"
gl_sdk="${PS5_OPENGL_SDK:-$root/../ps5-opengl-sdk-0.3.0/sdk}"
vulkan_dir="${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}"
vulkan_driver="${PS5_VULKAN_DRIVER:-radv}"
out="$root/build/gl-archives"

nm="$sdk/bin/llvm-nm"
objcopy="$sdk/bin/llvm-objcopy"
[[ -x $nm && -x $objcopy ]] || { echo "error: no llvm tools under $sdk/bin" >&2; exit 2; }

script="$gl_sdk/lib/libPS5OpenGLCore33.a"
[[ -f $script ]] || { echo "error: no PS5 OpenGL SDK at $gl_sdk" >&2; exit 2; }

# The members of the SDK's GROUP script, in its order.
mapfile -t members < <(grep -oE 'lib[A-Za-z0-9_.+-]+\.a' "$script")
(( ${#members[@]} > 0 )) || { echo "error: $script names no archives" >&2; exit 2; }
for member in "${members[@]}"; do
    [[ -f $gl_sdk/lib/$member ]] || { echo "error: $gl_sdk/lib/$member missing" >&2; exit 2; }
done

# The collision set is defined against whichever Vulkan stack the title links:
# RADV embeds Mesa whole; ps5vk embeds the psbc driver plus the Vulkan runtime.
case $vulkan_driver in
    radv) vulkan_archives=("$vulkan_dir/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a") ;;
    ps5vk) vulkan_archives=(
        "$vulkan_dir/build/driver/ps5/libps5vk.ps5.a"
        "$vulkan_dir/.deps/native/vulkan-runtime/lib/libvk_runtime.ps5.a"
        "$vulkan_dir/build/driver/ps5/libpsbc_driver.ps5.a"
        "$vulkan_dir/.deps/native/psbc/lib/libpsbc_support.ps5.a") ;;
esac

stamp_inputs=("$script" "${vulkan_archives[@]}")
for member in "${members[@]}"; do stamp_inputs+=("$gl_sdk/lib/$member"); done
stamp=$(sha256sum "${stamp_inputs[@]}" | sha256sum | cut -d' ' -f1)
stamp_file="$out/.stamp"
if [[ -f $stamp_file && $(<"$stamp_file") == "$stamp" ]]; then
    for member in "${members[@]}"; do printf '%s\n' "$out/$member"; done
    exit 0
fi
rm -rf -- "$out"
mkdir -p "$out"

# Global definitions only: lowercase nm type letters are locals, which bind
# inside their own member and can never collide.
defined_globals() {
    "$nm" --defined-only "$1" 2>/dev/null \
        | awk '{ if (NF == 3 && $2 ~ /^[A-Z]$/) print $3 }' | sort -u
}

for archive in "${vulkan_archives[@]}"; do
    [[ -f $archive ]] || { echo "error: no Vulkan archive at $archive" >&2; exit 2; }
done
: > "$out/.vulkan.defs"
for archive in "${vulkan_archives[@]}"; do
    defined_globals "$archive" >> "$out/.vulkan.defs"
done
sort -u -o "$out/.vulkan.defs" "$out/.vulkan.defs"

# Every name both sides define, prefixed the same way in every GL member.
: > "$out/.gl.defs"
for member in "${members[@]}"; do
    defined_globals "$gl_sdk/lib/$member" >> "$out/.gl.defs"
done
sort -u -o "$out/.gl.defs" "$out/.gl.defs"
comm -12 "$out/.vulkan.defs" "$out/.gl.defs" > "$out/.dups"
dup_count=$(wc -l < "$out/.dups")
echo "==> [gl] $dup_count symbols collide with the Vulkan driver; namespacing" >&2
: > "$out/.redefine.map"
if (( dup_count > 0 )); then
    awk '{ print $1 " ps5gl_" $1 }' "$out/.dups" > "$out/.redefine.map"
fi

for member in "${members[@]}"; do
    "$objcopy" --redefine-syms="$out/.redefine.map" \
        "$gl_sdk/lib/$member" "$out/$member"
    printf '%s\n' "$out/$member"
done

rm -f -- "$out/.vulkan.defs" "$out/.gl.defs" "$out/.dups" "$out/.redefine.map"
printf '%s\n' "$stamp" > "$stamp_file"
