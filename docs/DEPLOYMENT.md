# Deployment

How code reaches the place it runs, and how the run is observed. Two rules shape
everything here: **deployment is not verification** — shipping is safe because
the same artifact was already verified, not because the console looks fine
afterwards — and **this repository does not configure a console**. It builds a
homebrew title folder and uploads it to a console that is already set up.

## Environments

| Environment | What it is | Who may change it |
| --- | --- | --- |
| Local | the development machine: host tests, cross-build, staging | anyone |
| Console | a jailbroken PS5 on the local network, with its own loader | the agent, when the prompt names the console |
| Release | the ZIP attached to a tag, and the console a stranger installs it on | humans, deliberately |

The agent never writes outside `/data/homebrew/` on a console it was pointed at,
never installs or removes a title registration, and never touches a console whose
address is not in the ignored `.env`. Firmware, exploit and loader configuration
are the console owner's, not this project's.

## The artifact

Build once, stage once, promote the same tree. The artifact is
`dist/<TITLE_ID>/`: the signed application image `eboot.bin`, the runtime the
title carries in `sce_module/libc.prx`, and the metadata under `sce_sys/`
(`param.json`, `icon0.png`, `pic0.dds`, `pic1.dds`, `snd0.at9`) — exactly what the
console's loader reads, and nothing that only exists on this host. There is no
separate payload file: this is a title, and the console runs its `eboot.bin`.

The build is reproducible from a clean checkout at the committed revision:
`tools/fetch-retroarch.sh` names the upstream revision and checks the fetched tree
against that commit, `patches/series` is committed, and the artifact's file list,
sizes and sha256 digests are recorded in `dist/<TITLE_ID>/manifest.sha256`, which
`tools/check-manifest.sh` verifies. Anything that cannot be reproduced is not part
of the artifact (`vendor/`, `build/`, `.deps/`, `klog/`).

## Shipping a change

1. All gates green at the revision being shipped (`tools/verify.sh`).
2. `bash tools/build-title.sh` produces `dist/<TITLE_ID>/` and records its
   manifest; `bash tools/check-manifest.sh` proves the folder is that manifest —
   every file present and unchanged, nothing extra, `eboot.bin` a fake self, and
   `param.json` naming the folder it sits in.
3. `python3 tools/deploy-title.py` uploads the tree over the console's FTP
   service, each file under a temporary name and promoted only after its transfer
   completes, the image and the metadata last; it then reads every file back and
   compares digests.
4. `tools/console-run.sh <TITLE_ID>` starts the title through the resident control
   payload and captures the console's log from a marked position.
5. The run's distilled capture becomes the step's evidence under
   `evidence/<step>/`.

Selecting a console is configuration, never a committed value:

| Variable | Default | Purpose |
| --- | --- | --- |
| `PS5_HOST` | required | console address, from the environment or `.env` |
| `FTP_PORT` | `2121` | the console's FTP service |
| `KLOG_PORT` | `3232` | the console's kernel-log service |
| `PS5_CTL_PORT` | `9111` | the resident control payload's port |
| `PS5_FTP_USER`, `PS5_FTP_PASSWORD` | unset | credentials, if the console's FTP service wants them |
| `DEPLOY_DRY_RUN` | `0` | `1` builds and prints the target without networking |

`tools/deploy-title.py` refuses to run without a host and refuses a host that is
not an address or hostname. It publishes `dist/<TITLE_ID>/` and then reads every
file back and compares its sha256 with the file here, because this console's FTP
service has served a file's old bytes under its new name and listed a stale size
for a file it had already replaced: a size check passed on content that was not
there. Rollback and removal are the same command family: `python3
tools/deploy-title.py --clean` removes only this title's directory.

## Rollback

`tools/deploy-title.py --clean` removes `/data/homebrew/<TITLE_ID>/` and leaves
the previous release ZIP as the fallback: extracting the previous release over the
same path restores it in one step. A change that cannot be rolled back this way —
anything that writes outside the title folder — is not shipped without a written
reason in `docs/PHASE_LOG.md`.

Removal is deliberately named **undeploy**, not uninstall: FTP removal does not
unregister a title from the console's shell database, and this project does not
do that either.

## Configuration and secrets

- Configuration lives in `retroarch.cfg` inside the title folder, seeded from
  `config/retroarch.cfg` in this repository. Keys that a step adds are recorded
  in `docs/REFERENCE.md` with their default.
- The console's address and credentials live in the ignored `.env`, never in a
  committed file, never echoed into a log, and never pasted into `docs/`. The
  committed `.env.example` holds placeholders and the dry-run defaults.
- Nothing in this repository is signed, and no key, ticket or account credential
  is ever created here.

## Observing a target run

- `tools/console-run.sh <TITLE_ID>` captures the console's kernel log for the run:
  it checks what the console is already running, starts the listener, records its
  position in the stream, launches, and judges only the lines after that mark. The
  raw capture stays in the ignored `klog/`; the distilled record is what gets
  committed.
- The resident control payload comes from the console tooling already in use in
  `../PS5_Vulkan`: it answers one command per connection (`ping`, `procs`,
  `launch <TITLE_ID>`, `kill <TITLE_ID>`, `restart <TITLE_ID>`) so a run is
  scripted rather than clicked. Keeping it out of this repository is deliberate:
  one console agent, one owner.
- A run is identified by the console's own boot session plus the git revision
  being deployed. Record both next to the result: the same run id recurs across
  restarts and means nothing on its own.
- A capture is only kept when the run it describes is named by the step's
  acceptance line. Anything else is a scratch run and belongs in `klog/`.

## Sharing the console

The console runs one title at a time, and two sessions cannot both use it: a
launch from a second session replaces whatever the first was running. This
machine's console is shared with the PS5_Vulkan work, so **ask before every
launch and every upload** — the rule the console's owner set, and the reason
`tools/console-run.sh` prints what it is about to do before it does it, and
refuses to launch into a busy console with `0x80940010`.

Two habits follow from that:

- Prepare everything that can be prepared offline — the build, the staged tree,
  the checks — and touch the console only for the part that genuinely needs it.
- Treat a run as a claim about the revision it was launched from. The launch
  records the revision in `evidence/<step>/capture.json`, so a run that belongs
  to a different revision is visible rather than implied.

## Buffered GPU timing with development logs

`tools/run-title.sh --gpu-profile 60 --watch 80` opts into a bounded in-memory
capture, then retrieves the profile and the current `retroarch.log` alongside
its kernel/trace capture. A normal run omits `--gpu-profile`. The runner checks
console availability before upload and again before launch, and validates the
frontend log's build identity to reject stale logs. See `docs/GPU_TIMING.md` for
measurement definitions and `tools/analyze-gpu-profile.py` for replay.

## Native audio test

`tools/run-title.sh --no-build --audio-test --watch 45` uses the ordinary build
with a one-shot console control file; no diagnostic binary is left installed.
It requires at least a 20-second watch window, announces alternating low-level
left/right tones, captures and validates `audio-test.json`, then captures the
normal frontend log after the test's port is closed and reopened by RetroArch.
The owner must separately confirm audible playback. Omit `--audio-test` for
ordinary launches; the runner clears any stale audio control file in either case.

## Where to put configurations, cores and content

`/app0` is the running title's mount path, not an FTP directory. For this title,
FTP uses `/data/homebrew/PPSA99169/` as the corresponding base directory:

| Use | FTP path | In RetroArch |
| --- | --- | --- |
| Cores | `/data/homebrew/PPSA99169/cores/` | `/app0/cores/` |
| ROMs/content | `/data/homebrew/PPSA99169/content/` | `/app0/content/` |
| BIOS/system files | `/data/homebrew/PPSA99169/system/` | `/app0/system/` |
| Live settings | `/data/homebrew/PPSA99169/config/retroarch.cfg` | `/app0/config/retroarch.cfg` |
| Save files/states | `savefiles/`, `savestates/` under the FTP base | `/app0/savefiles/`, `/app0/savestates/` |

The native frontend creates these directories. It installs the packaged
`retroarch.cfg` seed only when the live config does not exist. Ordinary updates
replace packaged files but do not delete the user's live config, cores, content
or saves. Full `--clean` deployment/removal is destructive to the entire title
folder, including these user files: back them up before explicitly requesting it.

“Load Content” starts at `/app0`, while “Load Core” starts at `/app0/cores`.
A directory can correctly be empty: this filesystem step installs no emulator
cores or ROMs. A core must be built for this PS5 port; copying a desktop `.so`
does not make it loadable. USB/data roots appear only when the title can open
them; FTP visibility alone does not prove title access to an external mount.

The seven app-managed directories use owner-authorized `0777`; `0775` still
denied actual FTP uploads on this console. Starting the current build repairs
older `0755`/`0775` directories.
`python3 tools/check-ftp-write.py` verifies upload/readback/cleanup in every managed
folder with uniquely named disposable files and records the result under `klog/`.
It does not overwrite cores, ROMs, saved settings or other existing files.
