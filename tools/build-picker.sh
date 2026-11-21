#!/usr/bin/env bash
# PS5 RetroArch - build the frontend picker, /app0/picker/picker.bin (docs/FRONTENDS.md).
#
# The picker is a program of ../PS5_VulkanTemplate's UI module (frontends/picker/
# picker.cpp, in the kit's Gloss theme), so it is built the template's way, as the
# one program of a title the template generates:
#
#   1. ps5/tools/new-title.py --ui makes that title in build/picker/title, from the
#      template at the commit pinned here (it exports the commit, not the working
#      tree), with the template's .deps (the kit and the payload SDK it pins);
#   2. picker.cpp replaces the generated program, and three edits are made to the
#      generated build, each checked: its files live in /app0/picker (PS5_APP_ROOT
#      and the asset and shader folders), and its main calls ps5_title_next once
#      the program has ended, which restarts the title as the frontend chosen;
#   3. the template's ps5/tools/build.sh builds it against ../PS5_Vulkan (RADV and
#      the libc.prx this title also carries), and stages the notices of what it is
#      built from, reading those repositories beside this project's folder;
#   4. its eboot.bin is staged as picker/picker.bin, with the kit's assets and
#      shaders, and the two previews (assets/picker/*.png) as raw RGBA the picker
#      uploads as they are.
#
# Run by tools/build-title.sh when PS5_FRONTENDS names "picker", after the title
# folder is assembled. Inputs, overridable: PS5_VULKAN_TEMPLATE, PS5_VULKAN_DIR.
set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"
template=$(cd -- "${PS5_VULKAN_TEMPLATE:-$root/../PS5_VulkanTemplate}" && pwd)
vulkan=$(cd -- "${PS5_VULKAN_DIR:-$(bash "$root/tools/setup-vulkan.sh")}" && pwd)
template_commit=b577e950684e8bb835746ad0e38495ebcb154fd0
program=frontendpicker
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$root/sce_sys/param.json")
app="$root/dist/$title_id"
[[ -f $app/eboot.bin ]] || { echo "error: build the title first (no $app/eboot.bin)" >&2; exit 2; }
[[ $(git -C "$template" rev-parse HEAD) == "$template_commit" ]] ||
    { echo "error: $template is not at the pinned commit $template_commit" >&2; exit 2; }
for dependency in "$template/.deps/hui/src" "$template/.deps/native/ps5-payload-sdk/bin"; do
    [[ -d $dependency ]] || { echo "error: $dependency is missing (run the template's bootstrap)" >&2; exit 2; }
done

work="$root/build/picker"
generated="$work/title"
if [[ ! -f $generated/.picker-template || $(<"$generated/.picker-template") != "$template_commit" ]]; then
    echo "==> [picker] generating its title from PS5_VulkanTemplate ${template_commit:0:8}"
    rm -rf -- "$generated"
    mkdir -p "$work"
    python3 "$template/ps5/tools/new-title.py" "$generated" --title-id PPSA99168 --name "Frontend Picker" \
        --ui aurora > "$work/generate.log" 2>&1 || { cat "$work/generate.log" >&2; exit 1; }
    [[ -f $generated/examples/$program/$program.cpp ]] ||
        { echo "error: the template did not generate examples/$program" >&2; exit 1; }
    for file in ps5/CMakeLists.txt ps5/src/main.cpp; do
        cp "$generated/$file" "$generated/$file.generated"
    done
    ln -s "$template/.deps" "$generated/.deps"
    printf '%s\n' "$template_commit" > "$generated/.picker-template"
fi

cp "$root/frontends/picker/picker.cpp" "$generated/examples/$program/$program.cpp"
# The remembered frontend's file (config/frontend.cfg) is read and written with the
# same header-only code as eboot.bin's dispatch and the WebUI.
cp "$root/src/ps5_frontend_choice.h" "$generated/examples/$program/ps5_frontend_choice.h"
# The link that keeps the WebUI's daemon up while the picker shows (src/webui_link.h).
cp "$root/src/webui_link.h" "$root/src/webui_link.cpp" "$generated/examples/$program/"
# The same runtime QR encoder as RetroArch and ES-DE, compiled as C outside the UI namespace.
cp "$root/src/ps5_webui_qr.h" "$root/src/ps5_webui_qr.c" "$generated/examples/$program/"
mkdir -p "$generated/examples/$program/qrcodegen"
cp "$root/third_party/qrcodegen/"*.{c,h} "$generated/examples/$program/qrcodegen/"
rm -rf -- "$generated/examples/$program/kit"
python3 - "$generated" <<'PY'
import sys
from pathlib import Path
generated = Path(sys.argv[1])
def edit(name, old, new):
    text = (generated / (name + ".generated")).read_text()
    if text.count(old) != 1:
        raise SystemExit(f"error: {name} no longer has the text this build edits: {old!r}")
    return text.replace(old, new)
cmake = edit("ps5/CMakeLists.txt", "\tset(app_root /app0)\n\tset(surface_platform _DIRECT2DISPLAY)",
             '\tset(app_root /app0/picker)\n\tset(surface_platform _DIRECT2DISPLAY "PS5_APP_ROOT=\\"/app0/picker\\"")')
cmake += '\n# Runtime WebUI address lookup must live outside the template program namespace.\n'
cmake += 'target_sources(samples PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/../examples/frontendpicker/ps5_webui_qr.c")\n'
(generated / "ps5/CMakeLists.txt").write_text(cmake)
main = (generated / "ps5/src/main.cpp.generated").read_text()
old = '\t\tstatus = runSample(ps5Samples[0], 0, "", false).ok ? 0 : 1;\n'
if main.count(old) != 1 or main.count("\nint main()\n") != 1:
    raise SystemExit("error: ps5/src/main.cpp no longer has the text this build edits")
main = main.replace(old, old + "\t\t// PS5 RetroArch: what runs after the program (frontends/picker/picker.cpp)\n"
                               "\t\tif (ps5_title_next) {\n\t\t\tps5_title_next();\n\t\t}\n")
main = main.replace("\nint main()\n", '\nextern "C" __attribute__((weak)) void ps5_title_next(void);\n\nint main()\n')
(generated / "ps5/src/main.cpp").write_text(main)
PY

# The template's notices step reads the repositories its title is built from beside
# the title's folder; they are this project's neighbours, linked as they stand.
for neighbour in PS5_VKHomebrewUI PS5_PayloadSDK PS5_Mesa PS5_LLVM; do
    [[ ! -d $root/../$neighbour || -e $work/$neighbour ]] || ln -s "$(cd -- "$root/../$neighbour" && pwd)" "$work/$neighbour"
done
# PS5_Vulkan is the one the picker links (the pinned clone, tools/setup-vulkan.sh).
ln -sfn "$vulkan" "$work/PS5_Vulkan"
echo "==> [picker] building (log in build/picker/build.log)"
PS5_VULKAN_DIR="$vulkan" bash "$generated/ps5/tools/build.sh" > "$work/build.log" 2>&1 ||
    { grep -E "error|Error" "$work/build.log" | head -20 >&2; exit 1; }
built="$generated/dist/PPSA99168"
[[ -f $built/eboot.bin ]] || { echo "error: no $built/eboot.bin" >&2; exit 1; }
cmp -s "$built/sce_module/libc.prx" "$app/sce_module/libc.prx" ||
    { echo "error: the picker's libc.prx is not the title's" >&2; exit 1; }

picker="$app/picker"
rm -rf -- "$picker"
mkdir -p "$picker/previews" "$picker/screenshots"
cp "$built/eboot.bin" "$picker/picker.bin"
cp -r "$built/assets" "$built/shaders" "$picker/"
# The kit's songs play only when a program asks for them, and the picker does not.
rm -rf -- "$picker/assets/hui/audio/music"
[[ ! -d $built/licenses ]] || cp -r "$built/licenses" "$picker/licenses"
python3 - "$root/assets/picker" "$picker/previews" <<'PY'
import struct, sys
from pathlib import Path
from PIL import Image
source, target = Path(sys.argv[1]), Path(sys.argv[2])
for name, out in (("retroarch-xmb.png", "retroarch.rgba"), ("esde-alekfull-snes.png", "es-de.rgba")):
    image = Image.open(source / name).convert("RGBA").resize((1280, 720), Image.LANCZOS)
    (target / out).write_bytes(b"PS5RGBA1" + struct.pack("<II", *image.size) + image.tobytes())
PY
cp "$root/assets/picker/NOTICE.md" "$picker/previews/NOTICE.md"
printf '==> [picker] picker/picker.bin %s bytes, assets %s, previews 2 (1280x720)\n' \
    "$(stat -c %s "$picker/picker.bin")" "$(du -sh "$picker" | cut -f1)"
