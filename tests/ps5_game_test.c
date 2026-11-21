/* Game mode's contract (src/ps5_game.c), on the host: the request and result files,
 * launch commands as frontends expand them, the checks a request must pass, the core
 * RetroArch's playlists associate with a content, and a launch the console refuses.
 * The cores folder and the request and result files are in the scratch folder named
 * at compile time (tests/test_ps5_game.py). */
#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/ps5_game.c"

static int exec_calls;
int sceSystemServiceLoadExec(const char *path, const char *const *argv)
{
    exec_calls++;
    assert(strcmp(path, PS5_GAME_EBOOT) == 0 && strcmp(argv[0], "--ps5-mode=game") == 0 &&
           !argv[1]);
    return -1; /* refused: the launch must report it and not wait */
}

static void touch(const char *path)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fclose(file);
}

static void write_text(const char *path, const char *text)
{
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs(text, file);
    fclose(file);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    const char *dir = argv[1];
    char path[2048], content[1024], core[1024], frontend[1024], playlists[1024];
    struct ps5_game game, back;
    int kind = -1;

    /* The file round trip, request and result. */
    memset(&game, 0, sizeof(game));
    snprintf(game.core, sizeof(game.core), "%s", "/app0/cores/snes9x_libretro.so");
    snprintf(game.content, sizeof(game.content), "%s",
             "/app0/content/SNES/Jungle Run (USA) [!].zip");
    snprintf(game.frontend, sizeof(game.frontend), "%s", "/app0/es-de/es-de.bin");
    snprintf(game.state, sizeof(game.state), "%s",
             "snes\t/app0/content/SNES/Jungle Run (USA) [!].zip");
    snprintf(path, sizeof(path), "%s/request.txt", dir);
    assert(ps5_game_write(path, &game, 0) == 0);
    assert(ps5_game_read(path, &back, &kind) == 0 && kind == 0);
    assert(strcmp(back.core, game.core) == 0 && strcmp(back.content, game.content) == 0 &&
           strcmp(back.frontend, game.frontend) == 0 && strcmp(back.state, game.state) == 0);
    game.status = 3;
    game.seconds = 1234;
    snprintf(game.error, sizeof(game.error), "%s", "none");
    assert(ps5_game_write(path, &game, 1) == 0);
    assert(ps5_game_read(path, &back, &kind) == 0 && kind == 1 && back.status == 3 &&
           back.seconds == 1234 && strcmp(back.error, "none") == 0);
    write_text(path, "not a game\nkind=request\n");
    assert(ps5_game_read(path, &back, &kind) != 0);
    write_text(path, "ps5-game 1\ncore=x\n");
    assert(ps5_game_read(path, &back, &kind) != 0); /* no kind */

    /* Launch commands, as frontends expand them. */
    assert(
        ps5_game_parse_command("/app0/eboot.bin -L /app0/cores/snes9x_libretro.so "
                               "/app0/content/Nintendo\\ -\\ SNES/Jungle\\ Run\\ 2\\ \\(USA\\).zip",
                               &game) == 0);
    assert(strcmp(game.core, "/app0/cores/snes9x_libretro.so") == 0);
    assert(strcmp(game.content, "/app0/content/Nintendo - SNES/Jungle Run 2 (USA).zip") == 0);
    assert(ps5_game_parse_command("/app0/eboot.bin \"/app0/content/a b/it's.iso\"", &game) == 0 &&
           !game.core[0] && strcmp(game.content, "/app0/content/a b/it's.iso") == 0);
    assert(ps5_game_parse_command(
               "/app0/eboot.bin '/app0/x \"y\".bin' -L /app0/cores/a_libretro.so", &game) == 0 &&
           strcmp(game.content, "/app0/x \"y\".bin") == 0 &&
           strcmp(game.core, "/app0/cores/a_libretro.so") == 0);
    assert(ps5_game_parse_command("/usr/bin/retroarch -L a b", &game) != 0);
    assert(ps5_game_parse_command("/app0/eboot.bin -L /app0/cores/a_libretro.so", &game) !=
           0);                                                         /* no content */
    assert(ps5_game_parse_command("/app0/eboot.bin a b", &game) != 0); /* two */
    assert(ps5_game_parse_command("/app0/eboot.bin --verbose a", &game) != 0);
    assert(ps5_game_parse_command("/app0/eboot.bin \"unterminated", &game) != 0);
    assert(ps5_game_parse_command("/app0/eboot.bin -L", &game) != 0);

    /* The checks a request must pass. */
    snprintf(content, sizeof(content), "%s/game.zip", dir);
    snprintf(core, sizeof(core), "%sgood_libretro.so", PS5_GAME_CORES);
    snprintf(frontend, sizeof(frontend), "%s/frontend.bin", dir);
    mkdir(PS5_GAME_CORES, 0777);
    touch(content);
    touch(core);
    touch(frontend);
    memset(&game, 0, sizeof(game));
    snprintf(game.content, sizeof(game.content), "%s", content);
    snprintf(game.core, sizeof(game.core), "%s", core);
    snprintf(game.frontend, sizeof(game.frontend), "%s", frontend);
    assert(ps5_game_check(&game) == 0);
    game.core[0] = '\0';
    assert(ps5_game_check(&game) == 0); /* no core: the playlists choose */
    snprintf(game.core, sizeof(game.core), "%smissing_libretro.so", PS5_GAME_CORES);
    assert(ps5_game_check(&game) != 0);
    snprintf(game.core, sizeof(game.core), "%s", content);
    assert(ps5_game_check(&game) != 0); /* not in the cores folder */
    snprintf(game.core, sizeof(game.core), "%s", core);
    /* A file inside an archive that is there (issue 108), and inside one that is not. */
    snprintf(game.content, sizeof(game.content), "%.900s#inside/game.sfc", content);
    assert(ps5_game_check(&game) == 0);
    snprintf(game.content, sizeof(game.content), "%.900s/none.zip#game.sfc", dir);
    assert(ps5_game_check(&game) != 0);
    snprintf(game.content, sizeof(game.content), "%s", "relative.zip");
    assert(ps5_game_check(&game) != 0);
    snprintf(game.content, sizeof(game.content), "%s", content);
    snprintf(game.state, sizeof(game.state), "%s", "two\nlines");
    assert(ps5_game_check(&game) != 0);
    game.state[0] = '\0';
    snprintf(game.frontend, sizeof(game.frontend), "%s/none.bin", dir);
    assert(ps5_game_check(&game) != 0);

    /* The core RetroArch's playlists associate with a content. */
    snprintf(playlists, sizeof(playlists), "%s/playlists", dir);
    mkdir(playlists, 0777);
    snprintf(path, sizeof(path), "%s/SNES.lpl", playlists);
    write_text(
        path,
        "{\n  \"version\": \"1.5\",\n  \"default_core_path\": \"\",\n  \"items\": [\n"
        "    {\"path\": \"/app0/content/a.zip\", \"label\": \"A\", \"core_path\": \"DETECT\"},\n"
        "    {\"label\": \"Caf\\u00e9 \\\"B\\\"\", \"subsystem_roms\": [\"x\", {\"y\": [1, 2]}],\n"
        "     \"path\": \"/app0/content/Caf\\u00e9\\/b.zip\", \"crc32\": \"00000000|crc\",\n"
        "     \"core_path\": \"/app0/cores/snes9x_libretro.so\", \"core_name\": \"Snes9x\"},\n"
        "    {\"path\": \"/app0/content/d.zip#d.sfc\", \"label\": \"D\",\n"
        "     \"core_path\": \"/app0/cores/bsnes_libretro.so\"}\n"
        "  ]\n}\n");
    snprintf(path, sizeof(path), "%s/notes.txt", playlists);
    write_text(path, "not a playlist");
    char found[1024];
    assert(ps5_game_playlist_core(playlists, "/app0/content/Caf\xc3\xa9/b.zip", found,
                                  sizeof(found)) == 1 &&
           strcmp(found, "/app0/cores/snes9x_libretro.so") == 0);
    assert(ps5_game_playlist_core(playlists, "/app0/content/a.zip", found, sizeof(found)) ==
           0); /* DETECT */
    assert(ps5_game_playlist_core(playlists, "/app0/content/none.zip", found, sizeof(found)) == 0);
    /* A playlist names a file inside an archive; a frontend starts the archive (issue 108). */
    assert(ps5_game_playlist_core(playlists, "/app0/content/d.zip", found, sizeof(found)) == 1 &&
           strcmp(found, "/app0/cores/bsnes_libretro.so") == 0);
    snprintf(path, sizeof(path), "%s/broken.lpl", playlists);
    write_text(path, "{\"items\": [{\"path\": \"/app0/content/c.zip\", \"core_path\": ");
    assert(ps5_game_playlist_core(playlists, "/app0/content/c.zip", found, sizeof(found)) == 0);
    assert(ps5_game_playlist_core("/nonexistent", "/app0/content/a.zip", found, sizeof(found)) ==
           0);

    /* A launch the console refuses: reported, the request removed, no wait. */
    memset(&game, 0, sizeof(game));
    snprintf(game.content, sizeof(game.content), "%s", content);
    snprintf(game.frontend, sizeof(game.frontend), "%s", frontend);
    assert(ps5_game_launch(&game) == -1 && exec_calls == 1 &&
           strstr(game.error, "did not restart"));
    struct stat status;
    assert(stat(PS5_GAME_REQUEST_PATH, &status) != 0);
    snprintf(game.content, sizeof(game.content), "%s", "relative");
    assert(ps5_game_launch(&game) == -1 && exec_calls == 1); /* refused before LoadExec */

    /* The result, taken by the frontend it belongs to, once. */
    memset(&game, 0, sizeof(game));
    snprintf(game.frontend, sizeof(game.frontend), "%s", frontend);
    game.seconds = 42;
    assert(ps5_game_write(PS5_GAME_RESULT_PATH, &game, 1) == 0);
    assert(ps5_game_take_result("/app0/other.bin", &back) == 0);
    assert(ps5_game_take_result(frontend, &back) == 1 && back.seconds == 42);
    assert(ps5_game_take_result(frontend, &back) == 0);

    puts("ps5_game: request and result files, launch commands, checks, playlist cores and a "
         "refused launch PASS");
    return 0;
}
