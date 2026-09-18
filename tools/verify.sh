#!/usr/bin/env bash
# The gate runner. It runs the commands AGENTS.md names, in that order, and
# stops at the first failure: a red gate is never carried forward.
#
#   tools/verify.sh            run every gate
#   tools/verify.sh --list     print the gates and their commands
#   tools/verify.sh unit       run one gate alone
#
# Each gate is a precondition plus the command docs/TESTING.md documents. Until
# a gate's script exists the gate reports the file it is waiting for and fails,
# which is how a project that has not built anything yet stays honest about it.
# Adding a check means adding it to the script a gate already calls, so this
# file changes only when the set of gates changes.
#
# Details of what each gate means and how to add to it: docs/TESTING.md.

set -euo pipefail

cd "$(dirname -- "${BASH_SOURCE[0]}")/.."

gates=(format unit build integration evidence)

usage() {
    echo "usage: ${0##*/} [--list] [gate]" >&2
    echo "gates: ${gates[*]}" >&2
    exit 2
}

# Report the file a gate is waiting for, naming the gate and pointing at the
# document that says what the gate must run.
require() {
    local file=$1
    if [[ ! -e $file ]]; then
        echo "gate '$gate' is not configured: $file does not exist" >&2
        echo "  docs/TESTING.md says what this gate must run" >&2
        return 1
    fi
}

gate_format() {
    require tools/lint-shell.sh || return 1
    require tools/lint-format.sh || return 1
    bash tools/lint-shell.sh
    bash tools/lint-format.sh
}

gate_unit() {
    require Makefile || return 1
    make test-unit
}

gate_build() {
    require Makefile || return 1
    make app
}

gate_integration() {
    require Makefile || return 1
    require tools/check-manifest.sh || return 1
    make test-integration
}

gate_evidence() {
    require tools/evidence.py || return 1
    python3 tools/evidence.py compare evidence/
}

case "${1:-}" in
    --list|-l)
        printf '%-12s %s\n' \
            format      'bash tools/lint-shell.sh && bash tools/lint-format.sh' \
            unit        'make test-unit' \
            build       'make app' \
            integration 'make test-integration' \
            evidence    'python3 tools/evidence.py compare evidence/'
        exit 0
        ;;
    -h|--help) usage ;;
esac

selected=("$@")
if (( ${#selected[@]} == 0 )); then
    selected=("${gates[@]}")
else
    for gate in "${selected[@]}"; do
        for known in "${gates[@]}"; do
            [[ $gate == "$known" ]] && continue 2
        done
        echo "unknown gate: $gate" >&2
        usage
    done
fi

failed=0
for gate in "${selected[@]}"; do
    printf '== %s\n' "$gate"
    started=$SECONDS
    if "gate_$gate"; then
        printf '   PASS (%ss)\n' "$((SECONDS - started))"
    else
        printf '   FAIL (%ss)\n' "$((SECONDS - started))" >&2
        failed=$gate
        break
    fi
done

if [[ $failed != 0 ]]; then
    echo "verify: FAIL at gate '$failed'" >&2
    exit 1
fi
echo "verify: PASS (${selected[*]})"
