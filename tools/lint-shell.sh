#!/usr/bin/env bash
# PS5 RetroArch - the format gate's first half: syntax, and the Python side.
#
#   bash tools/lint-shell.sh
#
# What this checks, and what it deliberately does not. There is no shellcheck and
# no shfmt on the machine this was written on, so this gate is not a style check;
# it is the check that a script parses. That is worth having on its own: a
# malformed tools/*.sh is discovered by whichever build step runs it, which on
# this project is often a 60-second compile or a deploy, and the failure then
# looks like a build failure rather than a typo in a file nobody was editing.
#
# The Python half is the same idea with `ast`, which imports nothing and runs
# nothing - a tool that has a syntax error and a side effect on import does not
# get to have the side effect here.
#
# Secrets are checked for, not read: .env is git-ignored and no committed file may
# name a console address or a credential. The rule is in AGENTS.md, and a grep is
# the cheapest way to keep it true.

set -euo pipefail

root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$root"

failures=0
note() { printf '  %s\n' "$*"; }
bad() { printf '  FAIL %s\n' "$*" >&2; failures=$((failures + 1)); }

# Files this project maintains. vendor/ and build/ are fetched or generated, and
# their syntax is not this project's responsibility.
mapfile -t shells < <(find tools -maxdepth 1 -type f -name '*.sh' | sort)
(( ${#shells[@]} )) || bad 'no shell scripts found under tools/'

echo "==> [lint-shell] syntax of ${#shells[@]} scripts"
for script in "${shells[@]}"; do
    if bash -n "$script" 2>/tmp/lint-shell.error; then
        note "ok   $script"
    else
        bad "$script: $(head -1 /tmp/lint-shell.error)"
    fi
done

mapfile -t pythons < <(find tools tests -maxdepth 1 -type f -name '*.py' 2>/dev/null | sort)
echo "==> [lint-shell] syntax of ${#pythons[@]} python tools"
for script in "${pythons[@]}"; do
    if python3 -c 'import ast,sys; ast.parse(open(sys.argv[1], encoding="utf-8").read())' "$script" 2>/tmp/lint-python.error; then
        note "ok   $script"
    else
        bad "$script: $(tail -1 /tmp/lint-python.error)"
    fi
done

echo "==> [lint-shell] editors and line endings"
# A script with CRLF or a stray byte-order mark fails on the console-facing side
# of this project in ways that read as "command not found" with no obvious cause.
while IFS= read -r file; do
    if LC_ALL=C grep -q $'\r' "$file"; then
        bad "$file: contains a carriage return (CRLF line ending)"
    fi
    if head -c 3 "$file" | LC_ALL=C grep -q $'\xef\xbb\xbf'; then
        bad "$file: starts with a byte-order mark"
    fi
done < <(find tools patches -type f \( -name '*.sh' -o -name '*.py' -o -name 'series' \) 2>/dev/null | sort)
note 'checked for CRLF and byte-order marks'

echo "==> [lint-shell] no committed console address or credential"
# The value itself is not printed: a gate that echoes a secret into a log has
# published it. Only the file and the key are named.
leaks=0
while IFS= read -r hit; do
    printf '  FAIL %s\n' "$hit" >&2
    leaks=$((leaks + 1))
done < <(grep -rlnE '(PS5_HOST|PS5_FTP_PASSWORD|KLOG_PORT|PS5_CTL_PORT)[[:space:]]*=[[:space:]]*[^[:space:]]' \
            --include='*.sh' --include='*.py' --include='*.md' --include='*.json' --include='*.yml' \
            tools docs patches README.md AGENTS.md Makefile 2>/dev/null | sort)
(( leaks == 0 )) || failures=$((failures + leaks))

if (( failures > 0 )); then
    printf 'lint-shell: %s problem(s)\n' "$failures" >&2
    exit 1
fi
echo "lint-shell: PASS"
