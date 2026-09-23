# Sourced by tools/build-<core>.sh: skip a core whose inputs have not changed.
#
#   core_stamp_skip NAME OUTPUT... -- INPUT...   exit 0 now when up to date
#   core_stamp_write NAME                         record the stamp after a build
#
# A core is rebuilt from a fresh extraction every time it is built, which is
# right, and it took most of a no-change rebuild (269 s, 255 of it in the six
# cores). The stamp is a hash of everything the core's build reads that this
# repository controls: its build script (which pins the revision and the archive
# digests), its patches and port tooling, the shared native loader files, the
# ABI checker and the SDK compiler wrappers. When the stamp matches and every
# staged output is present, the build is skipped. PS5_FORCE_CORES=1 builds anyway.

core_stamp_dir="$root/build/cores/stamps"

core_stamp_compute() {
    local inputs=("$@") path
    {
        printf 'core-stamp v1\n'
        for path in "${inputs[@]}"; do
            if [[ -d $path ]]; then
                find "$path" -type f -print0 | sort -z | xargs -0 -r sha256sum
            elif [[ -f $path ]]; then
                sha256sum "$path"
            else
                printf 'missing %s\n' "$path"
            fi
        done
    } | sha256sum | cut -d' ' -f1
}

core_stamp_skip() {
    local name=$1 outputs=() inputs=() path
    shift
    while (( $# )) && [[ $1 != -- ]]; do outputs+=("$1"); shift; done
    [[ ${1:-} == -- ]] && shift
    inputs=("$@"
        "$root/tooling/native/ps5-core.ld" "$root/tooling/native/core_cxx_runtime.cpp"
        "$root/tools/check-core.py" "$root/tools/core-stamp.sh" "$root/tooling/prospero-clang18"
        "$root/.deps/native/ps5-payload-sdk/bin/prospero-clang"
        "$root/.deps/native/ps5-payload-sdk/bin/prospero-clang++")
    core_stamp_value=$(core_stamp_compute "${inputs[@]}")
    core_stamp_name=$name
    [[ ${PS5_FORCE_CORES:-0} == 1 ]] && return 0
    [[ -f $core_stamp_dir/$name && $(<"$core_stamp_dir/$name") == "$core_stamp_value" ]] || return 0
    for path in "${outputs[@]}"; do
        [[ -e $path ]] || return 0
    done
    printf '==> [%s] up to date (stamp %s); not rebuilt\n' "$name" "${core_stamp_value:0:12}"
    exit 0
}

core_stamp_write() {
    mkdir -p "$core_stamp_dir"
    printf '%s\n' "$core_stamp_value" > "$core_stamp_dir/$core_stamp_name"
}

# ccache in front of the SDK compilers when it is installed, as the driver's
# builds do; PS5_DISABLE_CCACHE=1 opts out. A core that does rebuild then mostly
# replays its unchanged objects from the cache.
core_ccache=""
if [[ ${PS5_DISABLE_CCACHE:-0} != 1 ]] && command -v ccache >/dev/null 2>&1; then
    core_ccache=ccache
fi
