#!/usr/bin/env bash
#   Copyright (C) 2025 John Törnblom
#
# This file is free software; you can redistribute it and/or modify it
# under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; see the file COPYING. If not see
# <http://www.gnu.org/licenses/>.

VER="1.21.1"
URL="https://github.com/libretro/libretro-database/archive/refs/tags/v1.21.1.tar.gz"

SCRIPT_PATH="$(realpath "${BASH_SOURCE[0]}")"
SCRIPT_DIR="$(dirname "${SCRIPT_PATH}")"

TEMPDIR=$(mktemp -d)
trap 'rm -rf -- "$TEMPDIR"' EXIT

wget -O $TEMPDIR/libretro-database.tar.gz "${URL}" || exit 1
tar xf  $TEMPDIR/libretro-database.tar.gz -C $TEMPDIR || exit 1

cd $TEMPDIR/libretro-database-$VER || exit 1

mkdir -p "${SCRIPT_DIR}/.config/retroarch/database" || exit 1
mv rdb "${SCRIPT_DIR}/.config/retroarch/database/" || exit 1
