// Host-only serialization of the same option tables compiled into each core.
#include <cstdio>
#include <string>
#include <libretro.h>
static std::string quoted(const char *value)
{
    std::string out = "\"";
    for (const unsigned char c : std::string(value ? value : ""))
    {
        if (c == '"' || c == '\\')
            out += '\\';
        if (c < 32)
        {
            char escape[7];
            std::snprintf(escape, sizeof escape, "\\u%04x", c);
            out += escape;
        }
        else
            out += c;
    }
    return out + '"';
}
static void dump(const retro_core_option_v2_category *cats,
                 const retro_core_option_v2_definition *defs)
{
    std::printf("{\"categories\":[");
    for (auto *c = cats; c && c->key; ++c)
        std::printf("%s{\"key\":%s,\"label\":%s,\"description\":%s}", c == cats ? "" : ",",
                    quoted(c->key).c_str(), quoted(c->desc).c_str(), quoted(c->info).c_str());
    std::printf("],\"settings\":[");
    for (auto *s = defs; s->key; ++s)
    {
        std::printf("%s{\"key\":%s,\"label\":%s,\"description\":%s,\"category\":%s,\"default\":%s,"
                    "\"choices\":[",
                    s == defs ? "" : ",", quoted(s->key).c_str(),
                    quoted(s->desc_categorized ? s->desc_categorized : s->desc).c_str(),
                    quoted(s->info_categorized ? s->info_categorized : s->info).c_str(),
                    quoted(s->category_key).c_str(), quoted(s->default_value).c_str());
        for (size_t i = 0; i < RETRO_NUM_CORE_OPTION_VALUES_MAX && s->values[i].value; ++i)
            std::printf(
                "%s[%s,%s]", i ? "," : "", quoted(s->values[i].value).c_str(),
                quoted(s->values[i].label ? s->values[i].label : s->values[i].value).c_str());
        std::printf("]}");
    }
    std::puts("]}");
}
