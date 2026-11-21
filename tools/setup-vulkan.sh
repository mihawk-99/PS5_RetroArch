#!/usr/bin/env bash
# PS5_Vulkan and the RADV it builds, at the revisions pinned here.
#
# eboot.bin links RADV (my Mesa fork, ../PS5_Mesa) with my ../PS5_Vulkan's link
# recipe (tools/radv-link.sh, tooling/psbc). Both are pinned below and built in a
# clone of PS5_Vulkan of their own, .deps/native/ps5-vulkan, so a build never takes
# what the siblings' working trees hold: v4.1.0-beta-4's release check stopped on an
# uncommitted file in ../PS5_Vulkan, which the title had taken as it stood. RADV is
# built with this project's SDK, the one eboot.bin links it with.
#
# Prints the clone's path; tools/build-title.sh runs it unless PS5_VULKAN_DIR names
# another PS5_Vulkan (a driver being worked on).
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
vulkan_fork="${PS5_VULKAN_FORK:-$root/../PS5_Vulkan}"
mesa_fork="${PS5_MESA_FORK:-$root/../PS5_Mesa}"
vulkan_revision=7f492b4c6f67a4e8c8c67a4272cfeef22abea9cc
mesa_revision=75238ae4db3249803df3deeba47340f6de27962e
vulkan="$root/.deps/native/ps5-vulkan"
sdk="$root/.deps/native/ps5-payload-sdk"

[[ -f $sdk/.ps5-sdk-revision ]] || { echo "run tools/setup-native-dependencies.sh first" >&2; exit 2; }
[[ -d $vulkan/.git ]] || git clone -q --no-checkout "$vulkan_fork" "$vulkan"
if [[ $(git -C "$vulkan" rev-parse -q --verify HEAD 2>/dev/null || true) != "$vulkan_revision" ]]; then
    # The pin may be on a branch the fork only tracks from GitHub: take every ref.
    git -C "$vulkan" cat-file -e "$vulkan_revision^{commit}" 2>/dev/null ||
        git -C "$vulkan" fetch -q "$vulkan_fork" '+refs/*:refs/fork/*'
    git -C "$vulkan" cat-file -e "$vulkan_revision^{commit}" 2>/dev/null ||
        { echo "the PS5_Vulkan fork at $vulkan_fork does not have $vulkan_revision" >&2; exit 2; }
    git -C "$vulkan" checkout -q -f --detach "$vulkan_revision"
fi
[[ -z $(git -C "$vulkan" status --porcelain --untracked-files=no) ]] ||
    { echo "error: $vulkan has local changes; it holds the pinned revision only" >&2; exit 2; }

mkdir -p "$vulkan/.deps/native"
ln -sfn "$sdk" "$vulkan/.deps/native/ps5-payload-sdk"
PS5_MESA_FORK="$mesa_fork" PS5_MESA_REVISION="$mesa_revision" \
    bash "$vulkan/tools/build-radv.sh" release >&2
printf '%s\n' "$vulkan"
