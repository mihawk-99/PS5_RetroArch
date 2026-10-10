#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-3.0-or-later
# The host tools of the core and title jobs, in an ubuntu:26.04 container: README's (git, wget,
# unzip, make, patch, pkg-config, readelf, host compilers), what the core builds run (CMake,
# Ninja, autotools, Python 3), rev and flock for the SDK's scripts, and LLVM 22 as the unversioned
# clang, clang++ and llvm-config: /usr/bin/clang is the compiler tools/build-title.sh names
# (PS5_CLANG), and the SDK's wrappers run the LLVM its prospero-llvm-config finds, llvm-config
# when no llvm-config-15 to -21 is installed. LLVM 22 links what the project's checks expect:
# FreeBSD/PS5 core libraries (tools/check-core.py) and -z nodynamic-undefined-weak for the
# title's weak references. Its bin directory goes on PATH for the unversioned tools (llvm-objdump).
set -euo pipefail
apt-get update
apt-get install --yes --no-install-recommends \
    ca-certificates git curl wget unzip zip xz-utils bzip2 file make build-essential pkgconf \
    patch rsync ccache binutils cmake ninja-build autoconf automake libtool bison flex gperf \
    bsdextrautils util-linux xxd python3 python3-pip python3-yaml python3-jsonschema \
    clang-22 lld-22 llvm-22 libclang-rt-22-dev
git config --global --add safe.directory '*'
for tool in clang clang++ llvm-config; do
    ln -sf "../lib/llvm-22/bin/$tool" "/usr/bin/$tool"
done
for version in 15 16 17 18 19 20 21; do
    ! command -v "llvm-config-$version" >/dev/null || { echo "llvm-config-$version would be the SDK's" >&2; exit 2; }
done
llvm=/usr/lib/llvm-22/bin
[[ -n ${GITHUB_PATH:-} ]] && echo "$llvm" >> "$GITHUB_PATH"
for tool in "$llvm/llvm-objdump" "$llvm/ld.lld" \
    "$(clang --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a"; do
    [[ -e $tool ]] || { echo "missing $tool" >&2; exit 2; }
done
clang --version | head -n 1
llvm-config --version
