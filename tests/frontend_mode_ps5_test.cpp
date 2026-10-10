/* Which frontend a launch of eboot.bin starts (src/frontend_mode_ps5.cpp): the
 * decision for every launch, the mode argument as the console gives argv, and the
 * LoadExec each decision makes, against a LoadExec that replaces the process (a
 * thrown Replaced), refuses, or is accepted and ignored. argv[1] is a scratch
 * directory for the files the decision reads. */
#include <sys/stat.h>

#include <algorithm>
#include <cassert>
#include <cstdio>
#include <string>
#include <vector>
#include "../src/frontend_mode_ps5.cpp"

namespace
{
struct Replaced
{
};
enum class Exec
{
    replace,
    refuse,
    ignore
};
Exec exec_mode = Exec::replace;
int exec_calls = 0;
std::string exec_path, exec_argument;
std::vector<std::string> marks;
bool l1_held = false;
int l1_reads = 0;
bool read_l1()
{
    l1_reads++;
    return l1_held;
}

std::string read_file(const std::string &path)
{
    std::string text;
    if (std::FILE *file = std::fopen(path.c_str(), "rb"))
    {
        char buffer[256];
        size_t n;
        while ((n = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
            text.append(buffer, n);
        std::fclose(file);
    }
    return text;
}

void write_file(const std::string &path, const std::string &text)
{
    std::FILE *file = std::fopen(path.c_str(), "wb");
    assert(file);
    std::fputs(text.c_str(), file);
    std::fclose(file);
}

void touch(const std::string &path, bool present)
{
    if (!present)
    {
        std::remove(path.c_str());
        return;
    }
    std::FILE *file = std::fopen(path.c_str(), "wb");
    assert(file);
    std::fclose(file);
}

/* One launch: what it LoadExecs, or "retroarch" when it returns to run RetroArch. */
std::string launch(const ps5::frontend_mode::Paths &paths, std::vector<const char *> argv)
{
    argv.push_back(nullptr);
    exec_path.clear();
    try
    {
        ps5::frontend_mode::run(paths, static_cast<int>(argv.size() - 1),
                                const_cast<char **>(argv.data()), 0);
        return "retroarch";
    }
    catch (const Replaced &)
    {
        return exec_path;
    }
}
} // namespace

/* src/permissions_ps5.cpp: the repair is the title's; here it has nothing to wait for. */
extern "C" void ps5_permissions_settle()
{
}
extern "C" int sceSystemServiceLoadExec(const char *path, const char *const *argv)
{
    exec_calls++;
    exec_path = path;
    exec_argument = argv && argv[0] ? argv[0] : "(none)";
    /* "exit" closes the title, with no arguments; a restart has exactly one. */
    assert(std::string(path) == "exit" ? !argv : argv && argv[0] && !argv[1]);
    if (exec_mode == Exec::replace)
        throw Replaced{};
    return exec_mode == Exec::refuse ? -1 : 0;
}

/* The title's pad read (src/frontend_hold_ps5.cpp); the launches here use read_l1. */
extern "C" bool ps5_frontend_reopen_held(void)
{
    return false;
}

void ps5::debug::mark(const char *step) noexcept
{
    marks.emplace_back(step);
}
void ps5::debug::mark_value(const char *step, long long value) noexcept
{
    marks.emplace_back(std::string(step) + " " + std::to_string(value));
}

int main(int argc, char **argv)
{
    using ps5::frontend_mode::decide;
    using ps5::frontend_mode::Launch;
    using ps5::frontend_mode::Next;
    assert(argc == 2);
    const std::string dir = argv[1];

    /* The decision, for every launch. */
    for (unsigned state = 0; state < 32; state++)
    {
        for (const char *mode : {"", "retroarch", "es-de", "picker", "game", "quit", "other"})
            for (const char *choice : {"ask", "retroarch", "es-de"})
            {
                Launch launch;
                launch.mode = mode;
                launch.test_run = state & 1;
                launch.picker_test = state & 2;
                launch.picker_present = state & 4;
                launch.es_de_present = state & 8;
                launch.reopen_held = state & 16;
                launch.choice = choice;
                const Next next = decide(launch);
                const std::string m = mode, c = choice;
                if (m == "game")
                    assert(next == Next::game);
                else if (m == "es-de")
                    assert(next == (launch.es_de_present ? Next::es_de : Next::retroarch));
                else if (m == "picker")
                    assert(next == (launch.picker_present ? Next::picker : Next::retroarch));
                else if (m == "quit") /* a frontend quit: the picker, or closed when remembered */
                    assert(next ==
                           (c == "ask" && launch.picker_present ? Next::picker : Next::close));
                else if (!m.empty())
                    assert(next == Next::retroarch);
                else if (launch.picker_test && launch.picker_present)
                    assert(next == Next::picker);
                else if (launch.test_run)
                    assert(next == Next::retroarch);
                else if (!launch.reopen_held && c == "retroarch")
                    assert(next == Next::retroarch);
                else if (!launch.reopen_held && c == "es-de" && launch.es_de_present)
                    assert(next == Next::es_de);
                else
                    assert(next == (launch.picker_present ? Next::picker : Next::retroarch));
            }
    }

    /* The mode argument, as the console gives argv: no program name, and an empty
     * argument from the home screen. */
    {
        char empty[] = "", mode[] = "--ps5-mode=retroarch", other[] = "--ps5-relaunch=2";
        char *home[] = {empty, nullptr};
        char *chosen[] = {mode, nullptr};
        char *both[] = {other, mode, nullptr};
        assert(ps5::frontend_mode::mode_argument(1, home).empty());
        assert(ps5::frontend_mode::mode_argument(1, chosen) == "retroarch");
        assert(ps5::frontend_mode::mode_argument(2, both) == "retroarch");
        assert(ps5::frontend_mode::mode_argument(0, nullptr).empty());
        /* A session starts with no argument of this port: the home screen, a test run. */
        assert(ps5::frontend_mode::session_start(1, home) &&
               ps5::frontend_mode::session_start(0, nullptr));
        assert(!ps5::frontend_mode::session_start(1, chosen) &&
               !ps5::frontend_mode::session_start(2, both));
    }

    /* The launches, against files in the scratch directory. */
    const ps5::frontend_mode::Paths paths{
        dir + "/picker.bin",      dir + "/es-de.bin",          dir + "/test-run.txt",
        dir + "/picker-test.txt", dir + "/eboot.bin",          dir + "/game-request.txt",
        dir + "/game-result.txt", dir + "/playlists",          dir + "/content/",
        dir + "/info/",           dir + "/frontend.cfg",       dir + "/trace.txt",
        dir + "/retroarch.log",   dir + "/retroarch-game.log", read_l1};
    touch(paths.picker, true);
    touch(paths.es_de, true);
    touch(paths.test_run, false);
    touch(paths.picker_test, false);
    assert(launch(paths, {""}) == paths.picker && exec_argument.empty()); /* the home screen */
    assert(marks.back() == "frontend: mode '', test run 0, picker test 0, picker 1, es-de 1, "
                           "remembered ask, L1 0 -> picker");
    assert(l1_reads == 0); /* nothing remembered: the pad is not read */
    assert(launch(paths, {"--ps5-mode=retroarch"}) == "retroarch");
    assert(launch(paths, {"--ps5-mode=es-de"}) == paths.es_de);
    touch(paths.test_run, true);
    assert(launch(paths, {""}) == "retroarch"); /* a test run stays RetroArch */
    touch(paths.picker_test, true);
    assert(launch(paths, {""}) == paths.picker); /* unless the picker test is armed */
    touch(paths.picker, false);
    assert(launch(paths, {""}) == "retroarch"); /* a title without the picker */
    touch(paths.es_de, false);
    assert(launch(paths, {"--ps5-mode=es-de"}) == "retroarch");

    /* A LoadExec refused, or accepted but not replacing the process, runs RetroArch. */
    touch(paths.picker, true);
    touch(paths.test_run, false);
    touch(paths.picker_test, false);
    for (Exec mode : {Exec::refuse, Exec::ignore})
    {
        exec_mode = mode;
        const int before = exec_calls;
        assert(launch(paths, {""}) == "retroarch" && exec_calls == before + 1 &&
               exec_path == paths.picker);
        assert(marks.back().rfind(
                   "frontend: LoadExec did not replace the process; RetroArch runs; result", 0) ==
               0);
    }
    /* A frontend remembered (config/frontend.cfg): a launch from the home screen starts
     * it, unless L1 is held; a frontend quitting then closes the title. */
    touch(paths.test_run, false);
    touch(paths.picker_test, false);
    touch(paths.es_de, true);
    exec_mode = Exec::replace;
    assert(ps5_frontend_choice_write(paths.choice.c_str(), "es-de") == 0);
    assert(read_file(paths.choice) == "frontend_start = \"es-de\"\n");
    assert(launch(paths, {""}) == paths.es_de && l1_reads == 1);
    l1_held = true;
    assert(launch(paths, {""}) == paths.picker && l1_reads == 2); /* L1: the picker anyway */
    l1_held = false;
    assert(launch(paths, {"--ps5-mode=quit"}) == "exit" && exec_argument == "(none)");
    assert(launch(paths, {"--ps5-mode=es-de"}) == paths.es_de); /* a mode is not overridden */
    assert(ps5_frontend_choice_write(paths.choice.c_str(), "retroarch") == 0);
    assert(launch(paths, {""}) == "retroarch");
    touch(paths.test_run, true);
    const int reads_before = l1_reads;
    assert(launch(paths, {""}) == "retroarch" && l1_reads == reads_before); /* a test run */
    touch(paths.test_run, false);
    assert(ps5_frontend_choice_write(paths.choice.c_str(), "ask") == 0);
    assert(launch(paths, {"--ps5-mode=quit"}) == paths.picker);
    /* A file that says anything else is "ask"; an invalid choice is not written. */
    write_file(paths.choice, "frontend_start = \"evil\"\nother = \"1\"\n");
    assert(std::string(ps5_frontend_choice_read(paths.choice.c_str())) == "ask");
    assert(ps5_frontend_choice_write(paths.choice.c_str(), "evil") != 0);
    std::remove(paths.choice.c_str());
    assert(std::string(ps5_frontend_choice_read(paths.choice.c_str())) == "ask");
    /* The shell refusing to close the title leaves RetroArch to run. */
    assert(ps5_frontend_choice_write(paths.choice.c_str(), "es-de") == 0);
    exec_mode = Exec::refuse;
    assert(launch(paths, {"--ps5-mode=quit"}) == "retroarch" &&
           marks.back().rfind("frontend: the shell did not close the title", 0) == 0);
    exec_mode = Exec::replace;
    std::remove(paths.choice.c_str());

    /* Each session's trace is kept once, at its start only; RetroArch's log by mode. */
    write_file(paths.trace, "last session\n");
    {
        char handover[] = "--ps5-mode=quit", empty[] = "";
        char *carried[] = {handover, nullptr};
        char *home[] = {empty, nullptr};
        ps5::frontend_mode::start_session_logs(paths, 1, carried);
        assert(read_file(paths.trace) == "last session\n");
        ps5::frontend_mode::start_session_logs(paths, 1, home);
        assert(!ps5::frontend_mode::exists(paths.trace) &&
               read_file(dir + "/trace.1.txt") == "last session\n");
    }
    write_file(paths.retroarch_log, "retroarch\n");
    assert(ps5::frontend_mode::retroarch_log(paths) == paths.retroarch_log);
    assert(!ps5::frontend_mode::exists(paths.retroarch_log) &&
           read_file(dir + "/retroarch.1.log") == "retroarch\n");

    /* After RetroArch quits: back to the picker only when the picker started it. */
    for (const char *mode : {"", "retroarch", "es-de", "picker"})
        for (bool present : {false, true})
            assert(ps5::frontend_mode::back_to_picker(mode, present) ==
                   (present && std::string(mode) == "retroarch"));
    const auto quit = [&](const char *mode)
    {
        exec_path.clear();
        try
        {
            ps5::frontend_mode::after_retroarch(paths, mode, 0, 0, false);
            return std::string("closes");
        }
        catch (const Replaced &)
        {
            return exec_path;
        }
    };
    exec_mode = Exec::replace;
    assert(quit("retroarch") == paths.eboot && exec_argument == "--ps5-mode=quit");
    assert(quit("") == "closes"); /* a test run's RetroArch closes the title */
    touch(paths.picker, false);
    assert(quit("retroarch") == "closes");
    touch(paths.picker, true);
    exec_mode = Exec::refuse;
    assert(quit("retroarch") == "closes" &&
           marks.back().rfind("frontend: LoadExec did not replace the process; the title closes",
                              0) == 0);
    /* Game mode: the request taken once, its core from the request or the playlists,
     * refused back to the frontend, and the result and the frontend after RetroArch. */
    exec_mode = Exec::replace;
    const std::string content = dir + "/game.zip",
                      core = std::string(PS5_GAME_CORES) + "snes9x_libretro.so",
                      frontend = paths.es_de;
    touch(paths.es_de, true); /* the frontend to come back to */
    mkdir(PS5_GAME_CORES, 0777);
    touch(content, true);
    touch(core, true);
    const auto request = [&](const char *core_path, const char *content_path)
    {
        struct ps5_game game = {};
        std::snprintf(game.core, sizeof(game.core), "%s", core_path);
        std::snprintf(game.content, sizeof(game.content), "%s", content_path);
        std::snprintf(game.frontend, sizeof(game.frontend), "%s", frontend.c_str());
        std::snprintf(game.state, sizeof(game.state), "%s", "snes\tgame.zip");
        assert(ps5_game_write(paths.request.c_str(), &game, 0) == 0);
    };
    const auto result = [&]()
    {
        struct ps5_game game;
        int kind = -1;
        assert(ps5_game_read(paths.result.c_str(), &game, &kind) == 0 && kind == 1);
        std::remove(paths.result.c_str());
        return game;
    };
    request(core.c_str(), content.c_str());
    assert(launch(paths, {"--ps5-mode=game"}) == "retroarch");
    const struct ps5_game *running = ps5::frontend_mode::running_game();
    assert(running && core == running->core && content == running->content);
    assert(!ps5::frontend_mode::exists(paths.request)); /* a request runs once */
    /* A game's RetroArch logs apart from RetroArch's own. */
    write_file(paths.game_log, "last game\n");
    assert(ps5::frontend_mode::retroarch_log(paths) == paths.game_log &&
           read_file(dir + "/retroarch-game.1.log") == "last game\n");
    assert(quit("") == frontend); /* RetroArch quit: back to the frontend, with the result */
    struct ps5_game back = result();
    assert(back.status == 0 && back.seconds >= 0 && core == back.core &&
           std::string(back.state) == "snes\tgame.zip");
    assert(!ps5::frontend_mode::running_game());

    /* An update installed as RetroArch quit: no handover, in game mode or from the picker. */
    request(core.c_str(), content.c_str());
    assert(launch(paths, {"--ps5-mode=game"}) == "retroarch" && ps5::frontend_mode::running_game());
    exec_path.clear();
    ps5::frontend_mode::after_retroarch(paths, "game", 0, 0, true);
    assert(exec_path.empty() && !ps5::frontend_mode::running_game() &&
           marks.back() == "frontend: an update was installed; the title closes, no handover");
    ps5::frontend_mode::after_retroarch(paths, "retroarch", 0, 0, true);
    assert(exec_path.empty());

    /* RetroArch's playlist association wins over the frontend's core, if it is the title's. */
    const std::string other = std::string(PS5_GAME_CORES) + "bsnes_libretro.so";
    touch(other, true);
    request(core.c_str(), content.c_str());
    mkdir(paths.playlists.c_str(), 0777);
    std::FILE *associated = std::fopen((paths.playlists + "/SNES.lpl").c_str(), "w");
    assert(associated);
    std::fprintf(associated, "{\"items\":[{\"path\":\"%s\",\"core_path\":\"%s\"}]}",
                 content.c_str(), other.c_str());
    std::fclose(associated);
    assert(launch(paths, {"--ps5-mode=game"}) == "retroarch" &&
           other == ps5::frontend_mode::running_game()->core);
    assert(quit("") == frontend);
    result();
    associated = std::fopen((paths.playlists + "/SNES.lpl").c_str(), "w");
    std::fprintf(associated,
                 "{\"items\":[{\"path\":\"%s\",\"core_path\":\"/elsewhere/x_libretro.so\"}]}",
                 content.c_str());
    std::fclose(associated);
    request(core.c_str(), content.c_str());
    assert(launch(paths, {"--ps5-mode=game"}) == "retroarch" &&
           core == ps5::frontend_mode::running_game()->core);
    assert(quit("") == frontend);
    result();

    request("", content.c_str()); /* no core: the playlists' */
    std::FILE *playlist = std::fopen((paths.playlists + "/SNES.lpl").c_str(), "w");
    assert(playlist);
    std::fprintf(playlist, "{\"items\":[{\"path\":\"%s\",\"core_path\":\"%s\"}]}", content.c_str(),
                 core.c_str());
    std::fclose(playlist);
    assert(launch(paths, {"--ps5-mode=game"}) == "retroarch" &&
           ps5::frontend_mode::running_game() && core == ps5::frontend_mode::running_game()->core);
    assert(quit("") == frontend);
    result();

    std::remove((paths.playlists + "/SNES.lpl").c_str());
    request("", content.c_str()); /* no core anywhere: refused back to the frontend */
    assert(launch(paths, {"--ps5-mode=game"}) == frontend && !ps5::frontend_mode::running_game());
    back = result();
    assert(back.status == -1 && std::string(back.error).find("no core") == 0);

    request(core.c_str(), (dir + "/missing.zip").c_str()); /* content that is not there */
    assert(launch(paths, {"--ps5-mode=game"}) == frontend);
    assert(result().status == -1);

    assert(launch(paths, {"--ps5-mode=game"}) == "retroarch" &&
           !ps5::frontend_mode::running_game());
    assert(marks.back() == "game mode: no request; RetroArch runs");

    /* A home screen forwarder: --rom and --core, run as a game; afterwards the title as from
     * the home screen, or closed with --exit-after-game. */
    {
        using ps5::frontend_mode::forward_arguments;
        using ps5::frontend_mode::forward_content;
        using ps5::frontend_mode::forward_core;
        char rom[] = "--rom", rom_value[] = "SNES/Game (USA).zip", core_flag[] = "--core=snes9x",
             exit_flag[] = "--exit-after-game", dangling[] = "--core";
        char *full[] = {rom, rom_value, core_flag, exit_flag, nullptr};
        const ps5::frontend_mode::Forward parsed = forward_arguments(4, full);
        assert(parsed.rom == "SNES/Game (USA).zip" && parsed.core == "snes9x" && parsed.exit_after_game);
        char *missing[] = {dangling, nullptr};
        assert(forward_arguments(1, missing).core.empty() && forward_arguments(0, nullptr).rom.empty());
        assert(forward_content("SNES/Game.zip", "/app0/content/") == "/app0/content/SNES/Game.zip");
        assert(forward_content("/mnt/usb0/Game.zip\r\n", "/c/") == "/mnt/usb0/Game.zip");
        assert(forward_content("../Game.zip", "/c/").empty() && forward_content("a/../../x", "/c/").empty());
        assert(forward_content("a/..b.zip", "/c/") == "/c/a/..b.zip" && forward_content("", "/c/").empty());
        for (const char *name : {"snes9x", "snes9x_libretro", "snes9x_libretro.so"})
            assert(forward_core(name, "/app0/cores/") == "/app0/cores/snes9x_libretro.so");
        assert(forward_core("/app0/cores/mgba_libretro.so", "/x/") == "/app0/cores/mgba_libretro.so");
        assert(forward_core("", "/app0/cores/").empty() && forward_core("_libretro.so", "/c/").empty());
    }
    mkdir((dir + "/content").c_str(), 0777);
    mkdir((dir + "/content/SNES").c_str(), 0777);
    const std::string forwarded = dir + "/content/SNES/Game.zip";
    touch(forwarded, true);
    touch(paths.eboot, true); /* the title, which game mode's check takes as the frontend */
    touch(paths.picker, true);
    std::remove(paths.choice.c_str());
    exec_mode = Exec::replace;
    /* --core named: the game, with that core; the playlists do not override it */
    assert(launch(paths, {"--rom", "SNES/Game.zip", "--core", "snes9x"}) == "retroarch");
    const struct ps5_game *forward_game = ps5::frontend_mode::running_game();
    assert(forward_game && forwarded == forward_game->content && core == forward_game->core);
    assert(marks.back() == "forwarder: " + forwarded + " with " + core);
    assert(ps5::frontend_mode::retroarch_log(paths) == paths.game_log);
    /* RetroArch quit: the title as from the home screen, with no result for a frontend */
    std::remove(paths.result.c_str());
    assert(quit("") == paths.eboot && exec_argument.empty());
    assert(!ps5::frontend_mode::exists(paths.result) && !ps5::frontend_mode::running_game());
    /* --exit-after-game: the title closes (catchReturnFromMain's LoadExec("exit")) */
    {
        const std::string absolute = "--rom=" + forwarded;
        assert(launch(paths, {absolute.c_str(), "--core=snes9x_libretro.so", "--exit-after-game"}) ==
               "retroarch");
        assert(ps5::frontend_mode::running_game() && quit("") == "closes");
        assert(marks.back().find("the title closes") != std::string::npos);
    }
    /* no --core: RetroArch's playlists choose, as for a frontend's game */
    playlist = std::fopen((paths.playlists + "/SNES.lpl").c_str(), "w");
    assert(playlist);
    std::fprintf(playlist, "{\"items\":[{\"path\":\"%s\",\"core_path\":\"%s\"}]}",
                 forwarded.c_str(), other.c_str());
    std::fclose(playlist);
    assert(launch(paths, {"--rom", "SNES/Game.zip"}) == "retroarch" &&
           other == ps5::frontend_mode::running_game()->core);
    assert(quit("") == paths.eboot);
    std::remove((paths.playlists + "/SNES.lpl").c_str());
    /* not runnable: the trace says why, and the launch goes on as from the home screen */
    assert(launch(paths, {"--rom", "SNES/Game.zip"}) == paths.picker &&
           !ps5::frontend_mode::running_game());
    assert(std::find_if(marks.begin(), marks.end(), [](const std::string &m)
                        { return m.find("no core: neither --core") != std::string::npos; }) != marks.end());
    /* nor a playlist: the core whose .info lists the extension (a hack no playlist has) */
    mkdir(paths.info.c_str(), 0777);
    write_file(paths.info + "snes9x_libretro.info",
               "display_name = \"Snes9x\"\nsupported_extensions = \"smc|SFC|swc\"\n");
    write_file(paths.info + "bsnes_libretro.info", "supported_extensions = \"sfc|bs\"\n");
    write_file(paths.info + "genesis_plus_gx_libretro.info",
               "  supported_extensions = \"md|gen|cue\"\n");
    write_file(paths.info + "picodrive_libretro.info", "supported_extensions = \"md|32x|cue\"\n");
    write_file(paths.info + "notes.txt", "supported_extensions = \"sfc\"\n");
    const std::string hack = dir + "/content/SNES/Mario (patched).SFC";
    touch(hack, true);
    assert((ps5::frontend_mode::forward_extension_cores(paths.info, hack) ==
            std::vector<std::string>{"bsnes", "snes9x"}));
    assert(ps5::frontend_mode::forward_extension_cores(paths.info, dir + "/content/SNES/x.smc") ==
           std::vector<std::string>{"snes9x"});
    assert(ps5::frontend_mode::forward_extension_cores(paths.info, dir + "/content/a.b/noext")
               .empty());
    assert(ps5::frontend_mode::forward_extension_cores(paths.info, dir + "/content/x.sf").empty());
    assert(ps5::frontend_mode::forward_preferred_core(
               "/x/Sonic.md", {"genesis_plus_gx", "picodrive"}) == "genesis_plus_gx");
    assert(
        ps5::frontend_mode::forward_preferred_core("/x/Game.cue", {"genesis_plus_gx", "picodrive"})
            .empty());
    /* bsnes lists .sfc but is not one of the title's cores (no library): snes9x runs it */
    std::remove(other.c_str());
    assert(launch(paths, {"--rom", "SNES/Mario (patched).SFC"}) == "retroarch" &&
           core == ps5::frontend_mode::running_game()->core);
    assert(quit("") == paths.eboot);
    /* both are cores of the title: --core has to choose, and the trace names them */
    touch(other, true);
    assert(launch(paths, {"--rom", "SNES/Mario (patched).SFC"}) == paths.picker &&
           !ps5::frontend_mode::running_game());
    assert(std::find_if(marks.begin(), marks.end(),
                        [](const std::string &m) {
                            return m.find("several cores list its extension (bsnes, snes9x)") !=
                                   std::string::npos;
                        }) != marks.end());
    /* a Mega Drive cartridge both Sega cores list: Genesis Plus GX */
    const std::string genesis = std::string(PS5_GAME_CORES) + "genesis_plus_gx_libretro.so";
    touch(genesis, true);
    touch(std::string(PS5_GAME_CORES) + "picodrive_libretro.so", true);
    touch(dir + "/content/Sonic.md", true);
    assert(launch(paths, {"--rom", "Sonic.md"}) == "retroarch" &&
           genesis == ps5::frontend_mode::running_game()->core);
    assert(quit("") == paths.eboot);
    /* a folder, for a core that opens folders (its .info lists "/"): DOSBox Pure's DOS or
     * Windows installation */
    write_file(paths.info + "dosbox_pure_libretro.info",
               "supported_extensions = \"zip|dosz|exe|/\"\n");
    const std::string dosbox = std::string(PS5_GAME_CORES) + "dosbox_pure_libretro.so";
    touch(dosbox, true);
    mkdir((dir + "/content/dos").c_str(), 0777);
    mkdir((dir + "/content/dos/w95").c_str(), 0777);
    assert(ps5::frontend_mode::forward_folder_cores(paths.info) ==
           std::vector<std::string>{"dosbox_pure"});
    assert(ps5::frontend_mode::forward_content("dos/w95/", "/c/") == "/c/dos/w95");
    {
        const std::string w95 = "--rom=" + dir + "/content/dos/w95";
        assert(launch(paths, {w95.c_str(), "--core", "dosbox_pure_libretro",
                              "--exit-after-game"}) == "retroarch" &&
               dosbox == ps5::frontend_mode::running_game()->core &&
               dir + "/content/dos/w95" == ps5::frontend_mode::running_game()->content);
        assert(quit("") == "closes");
    }
    /* without --core: the one core that opens folders */
    assert(launch(paths, {"--rom", "dos/w95/"}) == "retroarch" &&
           dosbox == ps5::frontend_mode::running_game()->core);
    assert(quit("") == paths.eboot);
    /* a core that does not open folders is refused, naming those that do */
    assert(launch(paths, {"--rom", "dos/w95", "--core", "snes9x"}) == paths.picker &&
           !ps5::frontend_mode::running_game());
    const auto marked = [&](const char *text)
    {
        return std::find_if(marks.begin(), marks.end(), [&](const std::string &m)
                            { return m.find(text) != std::string::npos; }) != marks.end();
    };
    assert(
        marked("snes9x does not open folders (its .info lists no \"/\"; these do: dosbox_pure)"));
    /* a folder that is not there is still "not a file" */
    assert(launch(paths, {"--rom", "dos/w98", "--core", "dosbox_pure"}) == paths.picker &&
           marked("--rom dos/w98 --core dosbox_pure not run: the content is not a file"));
    for (const std::vector<const char *> &args :
         std::vector<std::vector<const char *>>{{"--rom", "SNES/Missing.zip", "--core", "snes9x"},
                                                {"--rom", "../Game.zip", "--core", "snes9x"},
                                                {"--rom", "SNES/Game.zip", "--core", "nosuchcore"}})
        assert(launch(paths, args) == paths.picker && !ps5::frontend_mode::running_game());
    /* a handover's mode wins over --rom */
    assert(launch(paths, {"--ps5-mode=es-de", "--rom", "SNES/Game.zip", "--core", "snes9x"}) ==
               paths.es_de &&
           !ps5::frontend_mode::running_game());

    std::puts(
        "frontend_mode_ps5: forwarder --rom/--core launches, cores by extension, folders, refusals "
        "and "
        "after-game PASS; "
        "decisions, mode argument, LoadExec targets, test runs and "
        "refused or ignored LoadExec PASS; the remembered frontend, L1, quit and closing PASS; "
        "session and per-mode logs PASS; back to the picker after RetroArch, no handover after "
        "an update; game mode requests, cores, refusals and results PASS");
    return 0;
}
