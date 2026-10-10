#!/usr/bin/env bash
# Filesystem boundaries for the RetroArch title. The SDK's directory/*at helpers
# reach these operations too, as do cores through their generated imports.
set -euo pipefail
for name in open fopen freopen mkdir stat lstat chmod chdir unlink rmdir remove rename utime utimes; do
    printf '%s ' "--wrap=$name"
done
printf '\n'
