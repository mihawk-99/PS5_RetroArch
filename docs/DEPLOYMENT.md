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
`dist/<TITLE_ID>/`: the payload ELF `retroarch.elf`, the configuration seed
`retroarch.cfg`, the metadata under `sce_sys/` (`param.json`, `icon0.png`), and
the runtime files the title needs beside it — exactly what the console's loader
reads, and nothing that only exists on this host.

The build is reproducible from a clean checkout at the committed revision: the
upstream tarball's digest is pinned, `patches/series` is committed, and a staged
tree's file list and digests are recorded in `dist/<TITLE_ID>/manifest.sha256`,
which `tools/check-manifest.sh` verifies. Anything that cannot be reproduced is
not part of the artifact (`vendor/`, `build/`, `.deps/`, `klog/`).

## Shipping a change

1. All gates green at the revision being shipped (`tools/verify.sh`).
2. `make stage` produces `dist/<TITLE_ID>/` and its manifest digest.
3. `tools/deploy.sh` uploads the tree over the console's FTP service, each file
   under a temporary name and promoted only after its transfer completes; the
   metadata and the ELF are published last.
4. `tools/console.sh launch` starts the title through the resident control
   payload, and `tools/console.sh klog` captures the run.
5. `tools/console.sh kill` closes it, and the distilled capture becomes the
   step's evidence under `evidence/<step>/`.

Selecting a console is configuration, never a committed value:

| Variable | Default | Purpose |
| --- | --- | --- |
| `PS5_HOST` | required | console address, from the environment or `.env` |
| `FTP_PORT` | `2121` | the console's FTP service |
| `KLOG_PORT` | `3232` | the console's kernel-log service |
| `PS5_CTL_PORT` | `9111` | the resident control payload's port |
| `PS5_FTP_USER`, `PS5_FTP_PASSWORD` | unset | credentials, if the console's FTP service wants them |
| `DEPLOY_DRY_RUN` | `0` | `1` builds and prints the target without networking |

`tools/deploy.sh` refuses to run without a host, refuses a host that is not an
address or hostname, and with `DEPLOY_DRY_RUN=1` never opens a socket. Rollback
and removal are the same command family: `tools/deploy.sh undeploy` removes only
this title's directory.

## Rollback

`tools/deploy.sh undeploy` removes `/data/homebrew/<TITLE_ID>/` and leaves the
previous release ZIP as the fallback: extracting the previous release over the
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

- `tools/console.sh klog` captures the console's kernel log for the run, and the
  run's own output is captured beside it. The raw capture stays in the ignored
  `klog/`; the distilled record is what gets committed.
- The resident control payload comes from the console tooling already in use in
  `../PS5_Vulkan`: it answers one command per connection (`ping`, `status`,
  `launch <TITLE_ID>`, `kill <TITLE_ID>`, `restart <TITLE_ID>`) so a run is
  scripted rather than clicked. Keeping it out of this repository is deliberate:
  one console agent, one owner.
- A run is identified by the console's own boot session plus the git revision
  being deployed. Record both next to the result: the same run id recurs across
  restarts and means nothing on its own.
- A capture is only kept when the run it describes is named by the step's
  acceptance line. Anything else is a scratch run and belongs in `klog/`.
