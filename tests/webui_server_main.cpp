/* Host harness for the same HTTP handlers shipped in the console title. */
#include "../src/webui_ps5.h"
#include "../vendor/retroarch/libretro-common/include/libretro.h"
#include <csignal>
#include <cstdlib>
#include <unistd.h>
static volatile sig_atomic_t stopped = 0;
static void stop(int)
{
    stopped = 1;
}
int main(int argc, char **argv)
{
    if (argc != 3)
        return 2;
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    if (!ps5_webui_start(argv[1], static_cast<unsigned short>(std::atoi(argv[2]))))
        return 1;
    const retro_core_option_v2_category categories[] = {{"video", "Video", "Picture choices"},
                                                        {nullptr, nullptr, nullptr}};
    retro_core_option_v2_definition definitions[2]{};
    definitions[0].key = "test_resolution";
    definitions[0].desc = "Resolution <test>";
    definitions[0].info = "A description with \"quotes\" and a newline\n.";
    definitions[0].category_key = "video";
    definitions[0].values[0] = {"1", "Native"};
    definitions[0].values[1] = {"2", "Double"};
    retro_core_options_v2 options{const_cast<retro_core_option_v2_category *>(categories),
                                  definitions};
    ps5_webui_core_options("Metadata test", &options);
    ps5_webui_core_options("../escape", &options);
    const retro_variable legacy[] = {{"test_mode", "Mode; normal|fast"}, {nullptr, nullptr}};
    ps5_webui_core_variables("Legacy test", legacy);
    while (!stopped)
        usleep(10000);
    ps5_webui_stop();
}
