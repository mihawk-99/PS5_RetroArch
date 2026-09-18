/* PS5 RetroArch - launcher manifest for the console's homebrew loader.

This is our version of the manifest the vendored recipe ships in
reference/ps5-retroarch/homebrew.js. The loader reads this file from the title
folder, shows the two lines of text below in its menu, and starts the payload
with the arguments and environment given here.

Two things matter and are deliberately identical to the recipe's manifest, since
both were arrived at by that project the hard way and are not ours to change
without measuring:

  - HOME is set to the folder the loader started us from. RetroArch writes its
    configuration and its core list under $HOME/.config/retroarch, so this is
    what keeps saves and settings next to the title instead of somewhere the
    console clears.
  - LD_LIBRARY_PATH points at the same folder, so a library placed beside the
    payload is found before anything else.

Copyright (C) 2026 Mihawk
SPDX-License-Identifier: GPL-3.0-or-later

Based on the manifest by John Törnblom (Copyright (C) 2024), distributed under
the same licence; see reference/ps5-retroarch/PROVENANCE.txt. */

async function main() {
    const PAYLOAD = window.workingDir + '/retroarch.elf';
    const ENV = {HOME: window.workingDir,
                 LD_LIBRARY_PATH: window.workingDir};
    const ARGS = ['-f', '-c', window.workingDir + '/retroarch.cfg'];

    return {
        mainText: "PS5 RetroArch",
        secondaryText: 'RetroArch frontend for the libretro API',
        onclick: async () => {
            return {
                path: PAYLOAD,
                args: ARGS,
                env: ENV
            };
        }
    };
}
