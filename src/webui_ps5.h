/* Copyright (C) 2026 Mihawk; SPDX-License-Identifier: GPL-3.0-or-later */
#pragma once
// The server owns no emulator state; saved WebUI settings load on next launch.
bool ps5_webui_start(const char *root, unsigned short port = 6769);
void ps5_webui_stop();

struct retro_core_options_v2;
struct retro_variable;
extern "C" void ps5_webui_core_options(const char *, const retro_core_options_v2 *);
extern "C" void ps5_webui_core_variables(const char *, const retro_variable *);
