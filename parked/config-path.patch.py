    (
        # The title's own command line, kept.
        #
        # This is the fault that hid every other one: the port passed its entry
        # point `-f -c /app0/retroarch.cfg --verbose --menu`, and RetroArch threw
        # all of it away. Content loading rebuilds a fresh argv from the
        # frontend's environment (`content_load_init_wrap`), and the frontend's
        # own environment offers no `-c`, so the pair rebuilt here was
        # `retroarch --menu`. With no config path, `config_load` parsed a NULL path
        # and the whole title ran on compiled defaults: `input_driver` was "null"
        # instead of the "ps5" the config names, so the input driver this project
        # had just written was never initialised and the pad did nothing; and
        # `--verbose` was dropped too, so nothing said so. The display worked
        # because the video driver's *default* happened to be ps5 already.
        #
        # The line below restores the config path when the frontend environment
        # does not name one. It is the title's own path, and the title has exactly
        # one: /app0 is where the console mounts the application image, so there is
        # nothing to make configurable here.
        "tasks/task_content.c",
        # The anchor is the last statement of the function, not its closing
        # brace: upstream leaves a blank line before the brace, so an anchor that
        # spans it does not match.
        "   if (args->flags & RARCH_MAIN_WRAP_FLAG_VERBOSE)\n"
        "      argv[(*argc)++] = strldup(\"-v\", sizeof(\"-v\"));\n",
        "   if (args->flags & RARCH_MAIN_WRAP_FLAG_VERBOSE)\n"
        "      argv[(*argc)++] = strldup(\"-v\", sizeof(\"-v\"));\n"
        "\n"
        "   /* Added by this port (patches/series, 0006): the frontend's own\n"
        "    * environment rebuilds this argv from scratch and does not carry the\n"
        "    * title's `-c`, which left RetroArch parsing a NULL config path and\n"
        "    * running every setting at its compiled default. The title has one\n"
        "    * config file and one place to keep it. */\n"
        "   if (!args->config_path)\n"
        "   {\n"
        "      argv[(*argc)++] = strldup(\"-c\", sizeof(\"-c\"));\n"
        "      argv[(*argc)++] = strdup(\"/app0/retroarch.cfg\");\n"
        "   }\n",
        "the title has exactly",
    ),
