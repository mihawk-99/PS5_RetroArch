#!/usr/bin/env bash
# The three Mesa utility sources the driver's archives reference but that
# ../PS5_Vulkan's PS5 build leaves out.
#
# Its tools/build-psbc-ps5.sh compiles the archive's own files plus a short
# support list (os_time, u_process, u_format_s3tc, ps5_agc_package,
# psbc_ps5_shims), and third_party/opengnm-psbc/toolchain/
# Makefile.opengnm-psbc-ps5 additionally filters these three out of UTIL_SRCS:
#
#   src/util/u_thread.c  u_thread_create, u_thread_setname, util_barrier_init,
#                        util_barrier_destroy, util_barrier_wait,
#                        util_thread_get_time_nano
#   src/util/anon_file.c os_create_anonymous_file
#   src/util/os_file.c   os_read_file
#
# Nothing in that tree defines them, which no executable link survives: a shared
# object may leave symbols undefined - ../PS5_Vulkan's libvulkan.so.1 links that
# way, and its shader compiler archive is only ever linked into one - while a
# title's link may not, which is why this project's link stopped on them.
#
# Mesa's own definitions are compiled here rather than reimplemented, and with
# the sibling's PS5 configuration (toolchain/opengnm-psbc-ps5.mak, whose
# SHARED_FLAGS are reproduced below): u_thread.c's util_barrier and its mtx_t and
# cnd_t come from that tree's own c11/threads.h, and the u_queue.ps5.o already
# inside libpsbc_driver.ps5.a was compiled against exactly those, so a
# reimplementation would have to match its layout by luck.
#
# Writes build/obj/mesa-util/*.o, prints their paths one per line, and checks
# each object defines the symbols it is named for.
#
#   tools/build-mesa-util.sh [sdk_root]
#
# sdk_root defaults to $PS5_PAYLOAD_SDK, then to .deps/native/ps5-payload-sdk.
# PS5_VULKAN_DIR overrides the sibling project's root.

set -euo pipefail

cd "$(dirname -- "${BASH_SOURCE[0]}")/.."
root=$PWD

sdk=${1:-${PS5_PAYLOAD_SDK:-$root/.deps/native/ps5-payload-sdk}}
# Same default as tools/build-title.sh, which passes this through to make app.
export PS5_CLANG=${PS5_CLANG:-/usr/bin/clang}
vulkan_dir=${PS5_VULKAN_DIR:-$root/../PS5_Vulkan}
third_party="$vulkan_dir/.deps/work/psbc-ps5/third_party"
mesa="$third_party/opengnm-psbc"
out="$root/build/obj/mesa-util"

sources=(
    src/util/u_thread.c
    src/util/anon_file.c
    src/util/os_file.c
)

# One line per source, the symbols the link needs from it.
symbols_for() {
    case ${1##*/} in
        u_thread.c)
            printf '%s\n' u_thread_create u_thread_setname util_barrier_init \
                util_barrier_destroy util_barrier_wait util_thread_get_time_nano
            ;;
        anon_file.c) printf '%s\n' os_create_anonymous_file ;;
        os_file.c) printf '%s\n' os_read_file ;;
        *) return 1 ;;
    esac
}

missing=()
for source in "${sources[@]}"; do
    [[ -f $mesa/$source ]] || missing+=("$mesa/$source")
done
[[ -d $third_party/opengnm/include ]] || missing+=("$third_party/opengnm/include")
[[ -d $third_party/Vulkan-Headers/include ]] || missing+=("$third_party/Vulkan-Headers/include")
[[ -x $sdk/bin/prospero-clang ]] || missing+=("$sdk/bin/prospero-clang")
if (( ${#missing[@]} )); then
    printf 'error: the driver sources this step compiles are missing.\n' >&2
    printf '       Build ../PS5_Vulkan first (tools/build-psbc-ps5.sh), or set\n' >&2
    printf '       PS5_VULKAN_DIR and PS5_PAYLOAD_SDK.\n' >&2
    printf '       missing: %s\n' "${missing[@]}" >&2
    exit 2
fi

# SHARED_FLAGS and CFLAGS from the sibling's toolchain/opengnm-psbc-ps5.mak,
# whose include paths are relative to the tree being compiled, hence the cd.
shared_flags=(
    -Iinclude/
    -Ilibpsbc/
    -I"$third_party/opengnm/include"
    -I"$third_party/Vulkan-Headers/include"
    -Isrc/
    -Isrc/amd
    -Isrc/amd/common
    -Isrc/amd/common/nir
    -Isrc/amd/compiler
    -Isrc/amd/vulkan
    -Isrc/amd/vulkan/nir
    -Isrc/vulkan/runtime
    -Isrc/vulkan/runtime/bvh
    -Isrc/vulkan/util
    -Isrc/compiler
    -Isrc/compiler/nir
    -Isrc/compiler/spirv
    -Isrc/gallium/include
    -Isrc/mesa
    -Isrc/mesa/main
    -Isrc/util
    -Icmd/psbc
    -Iinclude/mesa
    -D_GNU_SOURCE
    -D_XOPEN_SOURCE=700
    -DUTIL_ARCH_LITTLE_ENDIAN=1
    -DUTIL_ARCH_BIG_ENDIAN=0
    -DHAVE_STRUCT_TIMESPEC=1
    -DHAVE_PTHREAD=1
    # The sibling's mak does not set this because its PS5 build never compiles
    # u_thread.c at all. The console is FreeBSD, where that file's u_thread_setname
    # calls pthread_set_name_np, declared in <pthread_np.h> and exported by
    # libkernel - the SDK has both.
    -DHAVE_PTHREAD_NP_H=1
    -DHAVE_SYSCONF=1
    -DHAVE_FUNC_ATTRIBUTE_PACKED=1
    -Dalloca=__builtin_alloca
    -include
    strings.h
    -DBLAKE3_NO_SSE2
    -DBLAKE3_NO_SSE41
    -DBLAKE3_NO_AVX2
    -DBLAKE3_NO_AVX512
)

mkdir -p "$out"
objects=()
for source in "${sources[@]}"; do
    object="$out/${source##*/}"
    object=${object%.c}.o
    file_flags=("${shared_flags[@]}")
    case ${source##*/} in
        anon_file.c)
            # os_create_anonymous_file's FreeBSD path is shm_open(SHM_ANON, ...),
            # and sys/mman.h declares SHM_ANON only under __BSD_VISIBLE, which
            # -D_XOPEN_SOURCE=700 clears. ../PS5_Vulkan's tooling/psbc/support.mk
            # drops that define for os_time.c's usleep and u_process.c's
            # getprogname for the same reason, so this is its rule, one file on.
            kept=()
            for flag in "${file_flags[@]}"; do
                [[ $flag == -D_XOPEN_SOURCE=700 ]] || kept+=("$flag")
            done
            file_flags=(${kept+"${kept[@]}"})
            ;;
        os_file.c)
            # os_same_file_description's FreeBSD branch walks the kernel's file
            # table through sysctl(KERN_FILE) and struct xfile, which needs the
            # kernel-only kvaddr_t the SDK's headers do not have - a title cannot
            # read that table anyway. __ORBIS__ selects the branch after it, which
            # answers "cannot tell" with -1, and is the same switch the sibling's
            # mak uses (`src/util/futex.ps5.o: CFLAGS += -D__ORBIS__`). os_read_file
            # itself is unguarded.
            file_flags+=(-D__ORBIS__)
            ;;
    esac
    (cd "$mesa" && PS5_PAYLOAD_SDK="$sdk" sh "$root/tooling/prospero-clang18" \
        -std=gnu11 -O2 -g -Wall -fPIC -DOPENGNM_PSBC_ORBIS=1 \
        -Dstatic_assert=_Static_assert \
        -Wno-unused-function -Wno-unused-variable \
        -Wno-unreachable-code-generic-assoc \
        ${file_flags+"${file_flags[@]}"} \
        -c "$source" -o "$object") 2>&1 | sed "s|^|[$source] |" >&2
    [[ -f $object ]] || { echo "error: $source did not compile" >&2; exit 1; }

    # The link resolves these by name, so check the object really defines them
    # instead of trusting that the compile produced what this step is for.
    while read -r symbol; do
        if ! "$sdk/bin/llvm-nm" --defined-only "$object" | grep -qE " [TtWw] $symbol\$"; then
            echo "error: $object does not define $symbol" >&2
            exit 1
        fi
    done < <(symbols_for "$source")
    objects+=("$object")
done

printf '%s\n' "${objects[@]}"
