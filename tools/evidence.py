#!/usr/bin/env python3
# PS5 RetroArch - console capture evidence.
#
#   python3 tools/evidence.py distil <raw log> --step NAME --note "..." [--console-run ID]
#   python3 tools/evidence.py compare [evidence/]
#
# A raw console capture is kept out of the repository (klog/); what is committed
# is a small, machine-readable record under evidence/<step>/, together with the
# command that reproduces it and the expectations it is compared against. The
# point is that a stranger can tell pass from fail by reading it, without a
# console and without trusting a summary.
#
#   capture.json     what was run, on which revision, against which console
#   expectation.json what the capture must show
#
# compare re-reads every expectation and checks it against every capture in the
# same directory. It never contacts the console, so it is safe to run in the
# gates and while another session is using the machine.

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
EVIDENCE = ROOT / "evidence"
SAMPLES = ("OK", "FAIL", "WORKING")


def relative(path: Path) -> str:
    try:
        return str(path.relative_to(ROOT))
    except ValueError:
        return str(path)


def command(kind: str) -> str:
    """The command recorded for a raw capture, taken from its first line.

    The launcher tool prints what it is about to do before it does it, so the
    first line of the capture is the reproduction command. Falling back to the
    tool name keeps a hand-made capture usable.
    """
    for line in kind.splitlines():
        stripped = line.strip()
        if stripped.startswith("==>"):
            return stripped.split("==>", 1)[1].strip()
    return kind.strip().splitlines()[0] if kind.strip() else "(unknown)"


def distil(args: argparse.Namespace) -> int:
    raw = Path(args.raw)
    if not raw.is_file():
        raise SystemExit(f"no capture at {raw}")
    text = raw.read_text(encoding="utf-8", errors="replace")
    step_dir = EVIDENCE / args.step
    step_dir.mkdir(parents=True, exist_ok=True)

    capture = {
        "step": args.step,
        "recorded": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
        "console_run": args.console_run,
        "revision": args.revision,
        "command": args.command or "tools/console-run.sh <TITLE_ID>",
        "raw_capture": relative(raw),
        "raw_lines": len(text.splitlines()),
        "raw_bytes": len(text.encode("utf-8")),
        # The decisions, not the noise: the first lines often carry a library
        # warning that is not part of the result, so both are kept.
        "lines": text.splitlines()[: args.head],
    }
    (step_dir / "capture.json").write_text(json.dumps(capture, indent=2) + "\n",
                                           encoding="utf-8")

    expectation = {
        "step": args.step,
        "note": args.note,
        "must_contain": args.must_contain,
        "must_not_contain": args.must_not_contain,
        "human_check": args.human_check,
    }
    (step_dir / "expectation.json").write_text(
        json.dumps(expectation, indent=2) + "\n", encoding="utf-8")
    print(f"wrote {relative(step_dir)}/capture.json and expectation.json")
    print(json.dumps(capture, indent=2))
    return 0


def compare(args: argparse.Namespace) -> int:
    base = Path(args.directory) if args.directory else EVIDENCE
    if not base.is_dir():
        print(f"no evidence directory at {base}: nothing to replay (0 captures)")
        return 0
    steps = sorted(p for p in base.iterdir() if p.is_dir())
    if not steps:
        print(f"{relative(base)} holds no captures: nothing to replay (0 captures)")
        return 0

    failures = 0
    for step in steps:
        capture_file = step / "capture.json"
        expect_file = step / "expectation.json"
        if not capture_file.is_file() or not expect_file.is_file():
            print(f"{step.name}: incomplete evidence (capture.json and "
                  f"expectation.json are both required)")
            failures += 1
            continue
        capture = json.loads(capture_file.read_text(encoding="utf-8"))
        expect = json.loads(expect_file.read_text(encoding="utf-8"))
        body = "\n".join(capture.get("lines", []))

        problems = []
        for needle in expect.get("must_contain", []):
            if needle not in body:
                problems.append(f"missing from the capture: {needle!r}")
        for needle in expect.get("must_not_contain", []):
            if needle in body:
                problems.append(f"present but must not be: {needle!r}")

        # The distilled record is the evidence; the raw capture is a working file
        # in the ignored klog/ tree and is expected to age out. Its absence is
        # reported, never a failure -- otherwise the gate would go red for a
        # cleanup rather than for a change in behaviour.
        raw = ROOT / capture.get("raw_capture", "")
        raw_gone = bool(capture.get("raw_capture")) and not raw.is_file()
        if not capture.get("lines"):
            problems.append("the record holds no lines: nothing was distilled")

        status = "FAIL" if problems else "OK"
        print(f"{step.name}: {status} (raw {capture.get('raw_capture')}, "
              f"{capture.get('raw_lines')} lines)")
        if raw_gone:
            print(f"    note: the raw capture is not on disk any more (it lives in "
                  f"the ignored klog/ tree); the distilled lines above are the record")
        if problems:
            failures += 1
            for problem in problems:
                print(f"    {problem}")
        if expect.get("human_check"):
            print(f"    human check: {expect['human_check']}")

    print(f"{len(steps)} capture(s) replayed, {failures} failed")
    return 1 if failures else 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Distil console captures into committed evidence, and replay them.")
    sub = parser.add_subparsers(dest="action", required=True)

    d = sub.add_parser("distil", help="turn a raw console capture into evidence")
    d.add_argument("raw")
    d.add_argument("--step", required=True)
    d.add_argument("--note", default="")
    d.add_argument("--console-run", default="")
    d.add_argument("--revision", default="")
    d.add_argument("--command", default="")
    d.add_argument("--head", type=int, default=40)
    d.add_argument("--must-contain", action="append", default=[])
    d.add_argument("--must-not-contain", action="append", default=[])
    d.add_argument("--human-check", default="")
    d.set_defaults(func=distil)

    c = sub.add_parser("compare", help="replay every committed capture")
    c.add_argument("directory", nargs="?")
    c.set_defaults(func=compare)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
