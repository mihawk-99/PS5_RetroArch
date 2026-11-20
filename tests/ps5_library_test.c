/* The game library every frontend shows (src/ps5_library.c), on the host: a library
 * shaped like the base console's (a mixed playlist whose entries name no core beside
 * one-system playlists with their cores), a playlist RetroArch scanned against its
 * databases, its history left out, a game in two playlists, a system of its own, the
 * cores and their info, and the launch commands game mode parses back.
 * argv[1] is a scratch folder. */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#include "../src/ps5_game.h"
#include "../src/ps5_library.h"

int sceSystemServiceLoadExec(const char *path, const char *const *argv)
{
    (void)path;
    (void)argv;
    return -1;
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

static void core(const char *dir, const char *name, const char *database, const char *extensions)
{
    char path[2048], text[2048];
    snprintf(path, sizeof(path), "%s/cores/%s_libretro.so", dir, name);
    write_text(path, "");
    snprintf(path, sizeof(path), "%s/info/%s_libretro.info", dir, name);
    snprintf(text, sizeof(text),
             "display_name = \"%s\"\ncorename = \"%s\"\ndatabase_match_archive_member = \"false\"\n"
             "database = \"%s\"\nsupported_extensions = \"%s\"\n",
             name, name, database, extensions);
    write_text(path, text);
}

/* A file's text, at most size - 1 bytes of it. */
static const char *read_back(const char *path, char *out, size_t size)
{
    FILE *file = fopen(path, "rb");
    assert(file);
    out[fread(out, 1, size - 1, file)] = '\0';
    fclose(file);
    return out;
}

static const struct ps5_library_system *system_of(const struct ps5_library *library, const char *id)
{
    for (size_t i = 0; i < library->system_count; i++)
        if (strcmp(library->systems[i].id, id) == 0)
            return &library->systems[i];
    return NULL;
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *dir = argv[1];

    /* Platforms, by the names RetroArch and people give them. */
    assert(strcmp(ps5_library_platform("Nintendo - Super Nintendo Entertainment System.lpl"),
                  "snes") == 0);
    assert(strcmp(ps5_library_platform("Nintendo - Nintendo Entertainment System"), "nes") == 0);
    assert(strcmp(ps5_library_platform("Nintendo - Game Boy Color"), "gbc") == 0);
    assert(strcmp(ps5_library_platform("Nintendo - Game Boy"), "gb") == 0);
    assert(strcmp(ps5_library_platform("PS1"), "psx") == 0);
    assert(strcmp(ps5_library_platform("PS2.lpl"), "ps2") == 0);
    assert(strcmp(ps5_library_platform("Sony - PlayStation Portable"), "psp") == 0);
    assert(strcmp(ps5_library_platform("Nintendo 3DS.lpl"), "n3ds") == 0);
    assert(strcmp(ps5_library_platform("ARCADE - NeoGeo"), "neogeo") == 0);
    assert(strcmp(ps5_library_platform("ARCADE - CPS I, II & III"), "cps") == 0);
    assert(strcmp(ps5_library_platform("ARCADE - Others (FBNeo)"), "fbneo") == 0);
    assert(strcmp(ps5_library_platform("ARCADE - Sega System 16 & 32"), "arcade") == 0);
    assert(strcmp(ps5_library_platform("Genesis"), "genesis") == 0);
    assert(strcmp(ps5_library_platform("Sega - Mega Drive - Genesis"), "genesis") == 0);
    assert(!ps5_library_platform("content.lpl") && !ps5_library_platform("My Homebrew"));
    assert(strcmp(ps5_library_platform_database("psx"), "Sony - PlayStation") == 0);

    /* The library. */
    char path[2048];
    const char *folders[] = {"cores", "info", "playlists"};
    for (int i = 0; i < 3; i++)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, folders[i]);
        mkdir(path, 0777);
    }
    core(dir, "snes9x", "Nintendo - Super Nintendo Entertainment System|Nintendo - Satellaview",
         "smc|sfc");
    core(dir, "mgba", "Nintendo - Game Boy|Nintendo - Game Boy Color|Nintendo - Game Boy Advance",
         "gb|gbc|gba");
    core(dir, "mednafen_psx_hw", "Sony - PlayStation", "cue|bin|chd");
    core(dir, "ps5sx2", "Sony - PlayStation 2", "iso|chd");
    core(dir, "fbneo", "FBNeo - Arcade Games", "zip|7z");
    core(dir, "mame", "MAME", "zip|7z");
    snprintf(path, sizeof(path), "%s/info/missing_libretro.info", dir); /* info without a core */
    write_text(path, "display_name = \"missing\"\ndatabase = \"Sega - Saturn\"\n");

    snprintf(path, sizeof(path), "%s/playlists/content.lpl", dir);
    write_text(
        path,
        "{\"version\": \"1.5\", \"default_core_path\": \"\", \"items\": [\n"
        "{\"path\": \"/app0/content/Nintendo - Super Nintendo Entertainment System/Jungle "
        "Run 2 (USA).zip\","
        " \"label\": \"Jungle Run 2 (USA)\", \"core_path\": \"DETECT\", \"db_name\": "
        "\"content.lpl\"},\n"
        "{\"path\": \"/app0/content/PS1/Disc Run (USA).cue\", \"label\": \"Disc Run (USA)\","
        " \"core_path\": \"DETECT\", \"db_name\": \"content.lpl\"},\n"
        "{\"path\": \"/app0/content/PS1/Disc Run (USA).bin\", \"label\": \"Disc Run track\","
        " \"core_path\": \"DETECT\", \"db_name\": \"content.lpl\"},\n"
        "{\"path\": \"/app0/content/PS1/Wing Flight (USA).bin\", \"label\": \"\","
        " \"core_path\": \"DETECT\", \"db_name\": \"content.lpl\"},\n"
        "{\"path\": \"/app0/content/Nintendo - Game Boy/Block Drop (World).zip\", \"label\": "
        "\"Block Drop\","
        " \"core_path\": \"DETECT\", \"db_name\": \"content.lpl\"},\n"
        "{\"path\": \"/app0/content/ARCADE - CPS I, II & III/arcade1.zip\", \"label\": \"arcade1\","
        " \"core_path\": \"DETECT\", \"db_name\": \"content.lpl\"},\n"
        "{\"path\": \"/app0/content/Homebrew/demo.bin\", \"label\": \"Demo\","
        " \"core_path\": \"DETECT\", \"db_name\": \"content.lpl\"}\n]}\n");
    snprintf(path, sizeof(path), "%s/playlists/PS2.lpl", dir);
    snprintf(path + 1024, 1024, "%s/cores/ps5sx2_libretro.so", dir);
    {
        char text[4096];
        snprintf(text, sizeof(text),
                 "{\"default_core_path\": \"%s\", \"items\": ["
                 "{\"path\": \"/app0/content/PS2/AS II/Ancient Saga II (USA).iso\", \"label\": "
                 "\"Ancient Saga II\","
                 " \"core_path\": \"%s\", \"db_name\": \"PS2.lpl\"},"
                 "{\"path\": \"/app0/content/PS2/Brush Wolf (USA).iso\", \"label\": \"Brush Wolf\","
                 " \"core_path\": \"DETECT\", \"db_name\": \"PS2.lpl\"}]}",
                 path + 1024, path + 1024);
        write_text(path, text);
    }
    snprintf(path, sizeof(path), "%s/playlists/Nintendo - Super Nintendo Entertainment System.lpl",
             dir);
    write_text(path, "{\"items\": [{\"path\": \"/app0/content/Nintendo - Super Nintendo "
                     "Entertainment System/Jungle Run "
                     "2 (USA).zip\", \"label\": \"JR2 again\", \"db_name\": \"Nintendo - Super "
                     "Nintendo Entertainment "
                     "System.lpl\"}, {\"path\": \"/app0/content/Nintendo - Super Nintendo "
                     "Entertainment System/Lucky's Hero "
                     "(USA).sfc\", \"label\": \"Lucky's Hero\", \"core_path\": \"DETECT\", "
                     "\"db_name\": \"Nintendo - Super "
                     "Nintendo Entertainment System.lpl\"}]}");
    snprintf(path, sizeof(path), "%s/playlists/content_history.lpl", dir);
    write_text(path, "{\"items\": [{\"path\": \"/app0/content/elsewhere/history.zip\", "
                     "\"core_path\": \"DETECT\"}]}");

    /* What RetroArch remembers of play: favourites and history in builtin/, and a
     * runtime log per core and game. */
    snprintf(path, sizeof(path), "%s/playlists/builtin", dir);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s/playlists/builtin/content_favorites.lpl", dir);
    write_text(path, "{\"items\": [{\"path\": \"/app0/content/Nintendo - Super Nintendo "
                     "Entertainment System/Lucky's Hero (USA).sfc\", \"core_path\": \"DETECT\"},"
                     "{\"path\": \"/app0/content/not/in/the/library.zip\"}]}");
    char history[2048];
    snprintf(history, sizeof(history), "%s/playlists/builtin/content_history.lpl", dir);
    write_text(history, "{\"items\": [{\"path\": \"/app0/content/PS2/Brush Wolf (USA).iso\"},"
                        "{\"path\": \"/app0/content/PS1/Disc Run (USA).cue\"},"
                        "{\"path\": \"/app0/content/Nintendo - Super Nintendo Entertainment "
                        "System/Lucky's Hero (USA).sfc\"},"
                        "{\"path\": \"/app0/content/PS2/Brush Wolf (USA).iso\"}]}");
    snprintf(path, sizeof(path), "%s/playlists/logs", dir);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s/playlists/logs/snes9x", dir);
    mkdir(path, 0777);
    snprintf(path, sizeof(path), "%s/playlists/logs/snes9x/Lucky's Hero (USA).lrtl", dir);
    write_text(path,
               "{\n  \"version\": \"1.0\",\n  \"runtime\": \"1:05:07\",\n  \"last_played\": "
               "\"2026-10-01 10:00:00\",\n  \"play_count\": \"3\",\n  \"state_slot\": \"0\"\n}\n");

    char playlists[1024], info[1024], cores[1024];
    snprintf(playlists, sizeof(playlists), "%s/playlists", dir);
    snprintf(info, sizeof(info), "%s/info", dir);
    snprintf(cores, sizeof(cores), "%s/cores", dir);
    struct ps5_library library;
    assert(ps5_library_load(&library, playlists, info, cores) == 0);
    assert(library.core_count == 6); /* the info without its core left out */
    assert(library.game_count == 9); /* JR2 once, the history left out */
    assert(library.system_count == 6);

    const struct ps5_library_system *snes = system_of(&library, "snes");
    assert(snes && snes->known && snes->game_count == 2);
    assert(strcmp(snes->name, "Nintendo - Super Nintendo Entertainment System") == 0);
    snprintf(path, sizeof(path), "%s/snes9x_libretro.so", cores);
    assert(strcmp(snes->core, path) == 0); /* from the core info: no entry names one */
    assert(strcmp(snes->folder, "/app0/content/Nintendo - Super Nintendo Entertainment System") ==
           0);
    assert(strstr(snes->extensions, "zip") && strstr(snes->extensions, "sfc") &&
           strstr(snes->extensions, "smc"));
    const struct ps5_library_game *first = &library.games[snes->first_game];
    /* Playlists are read in name order: a game in two is the first one's. */
    assert(strcmp(first->label, "JR2 again") == 0 &&
           strcmp(first->playlist, "Nintendo - Super Nintendo Entertainment System.lpl") == 0);
    assert(strcmp(library.games[snes->first_game + 1].label, "Lucky's Hero") == 0);

    const struct ps5_library_system *psx = system_of(&library, "psx");
    /* A disc's track, listed beside its .cue, is part of it; Wing Flight's lone .bin is a game. */
    assert(psx && psx->game_count == 2 && strcmp(psx->folder, "/app0/content/PS1") == 0);
    assert(strcmp(library.games[psx->first_game].label, "Disc Run (USA)") == 0);
    assert(strcmp(library.games[psx->first_game + 1].path,
                  "/app0/content/PS1/Wing Flight (USA).bin") == 0 &&
           strcmp(library.games[psx->first_game + 1].label, "Wing Flight (USA)") ==
               0); /* an empty label: the file's */

    const struct ps5_library_system *ps2 = system_of(&library, "ps2");
    assert(ps2 && ps2->game_count == 2 && strcmp(ps2->core, path + 1024) == 0);
    assert(strcmp(ps2->folder, "/app0/content/PS2") == 0); /* one game in a sub-folder */
    for (size_t i = 0; i < ps2->game_count; i++)
        assert(strcmp(library.games[ps2->first_game + i].core, ps2->core) ==
               0); /* DETECT: the playlist's */

    const struct ps5_library_system *gb = system_of(&library, "gb");
    snprintf(path, sizeof(path), "%s/mgba_libretro.so", cores);
    assert(gb && strcmp(gb->core, path) == 0);
    const struct ps5_library_system *cps = system_of(&library, "cps");
    snprintf(path, sizeof(path), "%s/fbneo_libretro.so", cores);
    assert(cps && cps->known && strcmp(cps->core, path) == 0); /* FBNeo first, then MAME */

    const struct ps5_library_system *homebrew = system_of(&library, "homebrew");
    assert(homebrew && !homebrew->known && strcmp(homebrew->name, "Homebrew") == 0 &&
           !homebrew->core[0]);

    /* What RetroArch remembers of play, with each game. */
    {
        const struct ps5_library_game *hero = &library.games[snes->first_game + 1];
        assert(hero->favorite && hero->history == 3 && hero->play_count == 3 &&
               hero->play_seconds == 3907 && strcmp(hero->last_played, "2026-10-01 10:00:00") == 0);
        const struct ps5_library_game *jr2 = &library.games[snes->first_game];
        assert(!jr2->favorite && !jr2->history && !jr2->last_played[0] && !jr2->play_count);
        /* No runtime log: a time from the place in the history, a minute a place. */
        struct stat written;
        assert(stat(history, &written) == 0);
        const struct ps5_library_game *brush = &library.games[ps2->first_game + 1];
        const struct ps5_library_game *disc = NULL;
        for (size_t i = 0; i < library.game_count; i++)
            if (strcmp(library.games[i].path, "/app0/content/PS1/Disc Run (USA).cue") == 0)
                disc = &library.games[i];
        assert(strcmp(brush->label, "Brush Wolf") == 0 && brush->history == 1 && disc &&
               disc->history == 2);
        char expected[20];
        struct tm local;
        time_t when = written.st_mtime;
        strftime(expected, sizeof(expected), "%Y-%m-%d %H:%M:%S", localtime_r(&when, &local));
        assert(strcmp(brush->last_played, expected) == 0);
        when -= 60;
        strftime(expected, sizeof(expected), "%Y-%m-%d %H:%M:%S", localtime_r(&when, &local));
        assert(strcmp(disc->last_played, expected) == 0);
    }

    /* Game mode's commands, parsed back. */
    char command[4096];
    struct ps5_game parsed;
    const struct ps5_library_game *hero = &library.games[snes->first_game + 1];
    assert(ps5_library_command(&library, hero, command, sizeof(command)) == 0);
    assert(ps5_game_parse_command(command, &parsed) == 0);
    assert(strcmp(parsed.content, hero->path) == 0 && strcmp(parsed.core, snes->core) == 0);
    const struct ps5_library_game *demo = &library.games[homebrew->first_game];
    assert(ps5_library_command(&library, demo, command, sizeof(command)) == 0);
    assert(ps5_game_parse_command(command, &parsed) == 0 && !parsed.core[0] &&
           strcmp(parsed.content, demo->path) == 0);
    assert(ps5_library_command(&library, hero, command, 20) == -1);

    ps5_library_free(&library);
    assert(ps5_library_load(&library, "/nonexistent", "/nonexistent", "/nonexistent") == 0 &&
           library.system_count == 0 && library.game_count == 0);
    ps5_library_free(&library);

    /* Issue 25: the games in the content folders that no playlist lists. */
    assert(strcmp(ps5_library_platform("NEC PC-FX"), "pcfx") == 0);
    assert(strcmp(ps5_library_platform("NeoGeo CD"), "neogeocd") == 0);
    assert(strcmp(ps5_library_platform("NeoGeo Pocket Color"), "ngpc") == 0);
    assert(strcmp(ps5_library_platform("Wonderswan Color"), "wonderswancolor") == 0);
    assert(strcmp(ps5_library_platform("NEC - Super Grafx"), "supergrafx") == 0);
    assert(strcmp(ps5_library_platform("ATARI 5200"), "atari5200") == 0);
    assert(strcmp(ps5_library_platform("Atari Jaguar"), "atarijaguar") == 0);
    assert(strcmp(ps5_library_platform("MS-DOS"), "dos") == 0);
    assert(strcmp(ps5_library_platform("3DO"), "3do") == 0);
    assert(strcmp(ps5_library_platform("Pokemon Mini"), "pokemini") == 0);
    assert(strcmp(ps5_library_platform("Virtual Boy"), "virtualboy") == 0);
    assert(strcmp(ps5_library_platform("ScummVM"), "scummvm") == 0);
    assert(strcmp(ps5_library_platform("ARCADE - NeoGeo"), "neogeo") == 0);
    core(dir, "stella", "Atari - 2600", "a26|bin"); /* its archives are RetroArch's to open */
    core(dir, "opera", "The 3DO Company - 3DO", "iso|bin|chd|cue");
    snprintf(path, sizeof(path), "%s/info/opera_libretro.info", dir);
    {
        FILE *info_file = fopen(path, "a");
        fputs("firmware0_path = \"panafz10.bin\"\nfirmware1_path = \"panafz1.bin\"\n", info_file);
        fclose(info_file);
    }
    core(dir, "dosbox_pure", "DOS", "zip|dosz|exe|com|bat|iso|cue");
    core(dir, "scummvm", "ScummVM", "scummvm");
    /* Issue 88 (PR 101): another system's core naming quest.dat and jungle.dat hides neither
     * Quest.zip nor Jungle Jump.a26, which ScummVM does not run. */
    snprintf(path, sizeof(path), "%s/info/scummvm_libretro.info", dir);
    {
        FILE *info_file = fopen(path, "a");
        fputs("firmware0_path = \"quest.dat\"\nfirmware1_path = \"jungle.dat\"\n", info_file);
        fclose(info_file);
    }
    /* a cartridge system whose core also runs a disc system (Sega CD) */
    core(dir, "genesis_plus_gx", "Sega - Mega Drive - Genesis|Sega - Mega-CD - Sega CD",
         "md|bin|cue|chd");
    const char *content_folders[] = {"content",
                                     "content/ATARI 2600",
                                     "content/ATARI 2600/Hacks",
                                     "content/3DO",
                                     "content/3DO/Game",
                                     "content/MS-DOS",
                                     "content/MS-DOS/shooter",
                                     "content/ScummVM",
                                     "content/ScummVM/Mansion",
                                     "content/ScummVM/Mansion/game",
                                     "content/My Stuff",
                                     "content/Saturn",
                                     "content/megadrive",
                                     "content/psx",
                                     "usb",
                                     "usb/Atari 2600"};
    for (size_t i = 0; i < sizeof(content_folders) / sizeof(content_folders[0]); i++)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, content_folders[i]);
        mkdir(path, 0777);
    }
    const char *content_files[] = {"content/ATARI 2600/Quest.zip",
                                   "content/ATARI 2600/readme.txt",
                                   "content/ATARI 2600/Hacks/Jungle Jump.a26",
                                   "content/3DO/panafz10.bin",
                                   "content/3DO/panafz10-patched.bin",
                                   "content/3DO/[BIOS] 3DO.bin",
                                   "content/3DO/Game/Game.cue",
                                   "content/3DO/Game/Game.iso",
                                   "content/MS-DOS/shooter/SHOOTER.EXE",
                                   "content/MS-DOS/shooter/SETUP.EXE",
                                   "content/MS-DOS/shooter/SHOOTER.WAD",
                                   "content/MS-DOS/Platform.zip",
                                   "content/ScummVM/Mansion/dosbox.conf",
                                   "content/ScummVM/Mansion/game/SETUP.BAT",
                                   "content/ScummVM/Mansion/game/MANSION.000",
                                   "content/My Stuff/thing.zip",
                                   "content/Saturn/game.cue",
                                   "usb/Atari 2600/River Run.a26",
                                   "content/3DO/Archived Disc.7z",
                                   "content/megadrive/Racer.7z",
                                   "content/psx/Archived Disc.7z",
                                   "usb/loose.a26"};
    for (size_t i = 0; i < sizeof(content_files) / sizeof(content_files[0]); i++)
    {
        snprintf(path, sizeof(path), "%s/%s", dir, content_files[i]);
        write_text(path, "x");
    }
    char content_root[1024], usb_root[1024];
    snprintf(content_root, sizeof(content_root), "%s/content", dir);
    snprintf(usb_root, sizeof(usb_root), "%s/usb", dir);
    const char *const roots[] = {content_root, usb_root, NULL};
    assert(ps5_library_load_content(&library, playlists, info, cores, roots) == 0);
    /* The playlists' nine, and: Quest, Jungle Jump, River Run (the drive), the 3DO game's
     * .cue, SHOOTER.EXE, Platform.zip, Mansion and Racer.7z (Quest.zip though Stella takes no
     * .zip: a cartridge system's archives are RetroArch's to open, and Racer.7z though its core
     * also runs Sega CD discs). Not: the 3DO's and PlayStation's archived discs (a disc
     * system's archives), readme.txt, the firmware (by the info's
     * names, a variant of one, [BIOS]), the .iso behind the .cue, SETUP.EXE, SHOOTER.WAD,
     * dosbox.conf, My Stuff (no platform), Saturn (no core), a file at a drive's top. */
    assert(library.game_count == 9 + 8);
    const struct ps5_library_system *megadrive = system_of(&library, "genesis");
    assert(megadrive && megadrive->game_count == 1 &&
           strstr(library.games[megadrive->first_game].path, "megadrive/Racer.7z"));
    const struct ps5_library_system *atari = system_of(&library, "atari2600");
    assert(atari && atari->game_count == 3 && strcmp(atari->folder, "/") != 0);
    const struct ps5_library_system *panasonic = system_of(&library, "3do");
    assert(panasonic && panasonic->game_count == 1);
    assert(strstr(library.games[panasonic->first_game].path, "3DO/Game/Game.cue"));
    assert(strstr(panasonic->core, "opera_libretro.so"));
    const struct ps5_library_system *dos = system_of(&library, "dos");
    assert(dos && dos->game_count == 2);
    for (size_t i = 0; i < dos->game_count; i++)
    {
        const struct ps5_library_game *game = &library.games[dos->first_game + i];
        assert(strstr(game->path, "shooter/SHOOTER.EXE")
                   ? strcmp(game->label, "shooter") == 0
                   : strstr(game->path, "Platform.zip") != NULL);
    }
    const struct ps5_library_system *scumm = system_of(&library, "scummvm");
    assert(scumm && scumm->game_count == 1);
    assert(strstr(library.games[scumm->first_game].path, "Mansion/game/MANSION.000") &&
           strcmp(library.games[scumm->first_game].label, "Mansion") == 0);
    assert(!system_of(&library, "saturn") && !system_of(&library, "mystuff"));
    /* A game a playlist lists is not listed twice, and keeps its playlist's place. */
    assert(system_of(&library, "snes")->game_count == 2);
    ps5_library_free(&library);

    /* A folder for each system the installed cores run, named as ES-DE names it; a system
     * with a folder already (any name) gets none. */
    {
        char root[1024], content[1100], info[1100], cores[1100], path[2048];
        snprintf(root, sizeof(root), "%s/folders", dir);
        snprintf(content, sizeof(content), "%s/content", root);
        snprintf(info, sizeof(info), "%s/info", root);
        snprintf(cores, sizeof(cores), "%s/cores", root);
        const char *folders[] = {root, content, info, cores};
        for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++)
            assert(mkdir(folders[i], 0755) == 0);
        snprintf(path, sizeof(path), "%s/PS1", content);
        assert(mkdir(path, 0755) == 0); /* the user's PlayStation folder */
        snprintf(path, sizeof(path), "%s/.hidden-snes", content);
        assert(mkdir(path, 0755) == 0); /* hidden: not a system folder */
        snprintf(path, sizeof(path), "%s/Genesis", content);
        write_text(path, "a file, not a folder");
        core(root, "mednafen_psx_hw", "Sony - PlayStation", "cue");
        core(root, "genesis_plus_gx",
             "Sega - Game Gear|Sega - Master System - Mark III|Sega - Mega-CD - Sega CD|"
             "Sega - Mega Drive - Genesis|Sega - PICO|Sega - SG-1000",
             "md");
        core(root, "snes9x",
             "Nintendo - Super Nintendo Entertainment System|Nintendo - Sufami Turbo|"
             "Nintendo - Satellaview",
             "sfc");
        core(root, "dolphin", "Nintendo - GameCube|Nintendo - Wii|Nintendo - Wii (Digital)", "iso");
        core(root, "puae", "Commodore - Amiga|Commodore - CD32|Commodore - CDTV", "adf");
        core(root, "fbneo", "FBNeo - Arcade Games", "zip");
        snprintf(path, sizeof(path), "%s/stella_libretro.info", info);
        write_text(path, "database = \"Atari - 2600\"\n"); /* no core beside it: not installed */
        assert(ps5_library_make_system_folders(content, info, cores) == 10);
        const char *expected[] = {"gamegear", "mastersystem", "segacd", "genesis", "sg-1000",
                                  "snes",     "gc",           "wii",    "amiga",   "fbneo"};
        for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); i++)
        {
            struct stat st;
            snprintf(path, sizeof(path), "%s/%s", content, expected[i]);
            assert(stat(path, &st) == 0 && S_ISDIR(st.st_mode) && (st.st_mode & 0777) == 0777);
            /* Each one is its system's to every frontend. */
            assert(ps5_library_platform(expected[i]));
        }
        const char *absent[] = {"Sony - PlayStation",
                                "psx",
                                "Nintendo - Sufami Turbo",
                                "Nintendo - Satellaview",
                                "Sega - PICO",
                                "Commodore - CD32",
                                "Atari - 2600",
                                "atari2600",
                                "Nintendo - Wii (Digital)",
                                "Sega - Mega Drive - Genesis"};
        for (size_t i = 0; i < sizeof(absent) / sizeof(absent[0]); i++)
        {
            struct stat st;
            snprintf(path, sizeof(path), "%s/%s", content, absent[i]);
            assert(stat(path, &st) != 0);
        }
        assert(ps5_library_make_system_folders(content, info, cores) == 0); /* once */
        /* An install copied without its empty folders: content is made too */
        snprintf(path, sizeof(path), "%s/missing", root);
        assert(ps5_library_make_system_folders(path, info, cores) == 11);
        snprintf(path, sizeof(path), "%s/missing/psx", root);
        assert(stat(path, &(struct stat){0}) == 0);
        snprintf(path, sizeof(path), "%s/no/such/content", root);
        assert(ps5_library_make_system_folders(path, info, cores) == -1);
    }
    /* Folders earlier builds named as RetroArch does, renamed as ES-DE names them, with the
     * playlists' paths and the per-folder settings that name them. */
    {
        char root[1024], content[1100], playlists[1100], config[1100], path[2048], text[4096],
            want[2048];
        snprintf(root, sizeof(root), "%s/renamed", dir);
        snprintf(content, sizeof(content), "%s/content", root);
        snprintf(playlists, sizeof(playlists), "%s/playlists", root);
        snprintf(config, sizeof(config), "%s/config", root);
        const char *folders[] = {"",
                                 "/content",
                                 "/content/Sony - PlayStation 2",
                                 "/content/Nintendo - Game Boy",
                                 "/content/gb",
                                 "/content/Nintendo - Game Boy Advance",
                                 "/content/gba",
                                 "/content/MAME",
                                 "/content/PS1",
                                 "/playlists",
                                 "/playlists/builtin",
                                 "/config",
                                 "/config/PS5SX2",
                                 "/config/MAME",
                                 "/config/remaps",
                                 "/config/remaps/PS5SX2"};
        for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); i++)
        {
            snprintf(path, sizeof(path), "%s%s", root, folders[i]);
            assert(mkdir(path, 0755) == 0);
        }
        const char *files[] = {"content/Sony - PlayStation 2/Disc.iso",
                               "content/Nintendo - Game Boy/One.gb",
                               "content/gb/Two.gb",
                               "content/Nintendo - Game Boy Advance/Same.gba",
                               "content/gba/Same.gba",
                               "content/MAME/game.zip",
                               "content/PS1/Disc.cue",
                               "config/PS5SX2/Sony - PlayStation 2.cfg",
                               "config/PS5SX2/Sony - PlayStation 2.opt",
                               "config/remaps/PS5SX2/Sony - PlayStation 2.rmp",
                               "config/MAME/MAME.cfg",
                               "config/MAME/MAME.slangp"};
        for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++)
        {
            snprintf(path, sizeof(path), "%s/%s", root, files[i]);
            write_text(path, "x");
        }
        snprintf(path, sizeof(path), "%s/Sony - PlayStation 2.lpl", playlists);
        snprintf(text, sizeof(text),
                 "{\"scan_content_dir\": \"%s/Sony - PlayStation 2\", \"items\": [{\"path\": "
                 "\"%s/Sony - PlayStation 2/Disc.iso\"}]}",
                 content, content);
        write_text(path, text);
        snprintf(path, sizeof(path), "%s/Nintendo - Game Boy.lpl", playlists);
        snprintf(text, sizeof(text),
                 "{\"items\": [{\"path\": \"%s/Nintendo - Game Boy/One.gb\"}, {\"path\": "
                 "\"%s/Nintendo - Game Boy Advance/Same.gba\"}]}",
                 content, content);
        write_text(path, text);
        snprintf(path, sizeof(path), "%s/builtin/content_history.lpl", playlists);
        snprintf(text, sizeof(text), "{\"items\": [{\"path\": \"%s/MAME/game.zip#game.bin\"}]}",
                 content);
        write_text(path, text);

        /* Renamed (ps2, mame), merged (gb), left where a name is taken (gba's), and a
         * folder that is not a name of RetroArch's untouched (PS1). */
        assert(ps5_library_rename_system_folders(content, playlists, config) == 3);
        const char *there[] = {
            "ps2/Disc.iso", "gb/One.gb",     "gb/Two.gb",   "Nintendo - Game Boy Advance/Same.gba",
            "gba/Same.gba", "mame/game.zip", "PS1/Disc.cue"};
        for (size_t i = 0; i < sizeof(there) / sizeof(there[0]); i++)
        {
            snprintf(path, sizeof(path), "%s/%s", content, there[i]);
            assert(stat(path, &(struct stat){0}) == 0);
        }
        const char *gone[] = {"Sony - PlayStation 2", "Nintendo - Game Boy", "MAME"};
        for (size_t i = 0; i < sizeof(gone) / sizeof(gone[0]); i++)
        {
            snprintf(path, sizeof(path), "%s/%s", content, gone[i]);
            assert(stat(path, &(struct stat){0}) != 0);
        }
        /* The playlists follow; a folder whose name only begins the same is left. */
        snprintf(path, sizeof(path), "%s/Sony - PlayStation 2.lpl", playlists);
        read_back(path, text, sizeof(text));
        snprintf(want, sizeof(want), "\"scan_content_dir\": \"%s/ps2\"", content);
        assert(strstr(text, want));
        snprintf(want, sizeof(want), "\"%s/ps2/Disc.iso\"", content);
        assert(strstr(text, want) && !strstr(text, "PlayStation 2/"));
        snprintf(path, sizeof(path), "%s/Nintendo - Game Boy.lpl", playlists);
        read_back(path, text, sizeof(text));
        snprintf(want, sizeof(want), "\"%s/gb/One.gb\"", content);
        assert(strstr(text, want));
        snprintf(want, sizeof(want), "\"%s/Nintendo - Game Boy Advance/Same.gba\"", content);
        assert(strstr(text, want));
        snprintf(path, sizeof(path), "%s/builtin/content_history.lpl", playlists);
        read_back(path, text, sizeof(text));
        snprintf(want, sizeof(want), "\"%s/mame/game.zip#game.bin\"", content);
        assert(strstr(text, want));
        /* The per-folder settings follow; a core named as the folder keeps its own. */
        const char *settings[] = {"PS5SX2/ps2.cfg", "PS5SX2/ps2.opt", "remaps/PS5SX2/ps2.rmp",
                                  "MAME/MAME.cfg", "MAME/MAME.slangp"};
        for (size_t i = 0; i < sizeof(settings) / sizeof(settings[0]); i++)
        {
            snprintf(path, sizeof(path), "%s/%s", config, settings[i]);
            assert(stat(path, &(struct stat){0}) == 0);
        }
        snprintf(path, sizeof(path), "%s/PS5SX2/Sony - PlayStation 2.cfg", config);
        assert(stat(path, &(struct stat){0}) != 0);
        snprintf(path, sizeof(path), "%s/MAME/mame.cfg", config);
        assert(stat(path, &(struct stat){0}) != 0);
        assert(ps5_library_rename_system_folders(content, playlists, config) == 0); /* once */
        snprintf(path, sizeof(path), "%s/none", root);
        assert(ps5_library_rename_system_folders(path, playlists, config) == -1);
    }
    puts("ps5_library: platforms, playlists, systems, cores, folders, labels, favourites, history, "
         "runtime logs, game mode commands, content folders, system folders and their ES-DE "
         "names PASS");
    return 0;
}
