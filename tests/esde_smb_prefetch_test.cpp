// Compile the production cache with deliberately slow/failing reads. No PS5 input.
#include "SmbImageCache.h"
#include <cassert>
#include <future>
#include <iostream>
#include <atomic>

using namespace SmbImages;
using namespace std::chrono_literals;
static Cache::Ptr image(size_t size = 64)
{
    auto p = std::make_shared<Image>(); p->width = 4; p->height = size / 16;
    p->pixels.assign(size, 42); return p;
}
template<class F> static void until(F f)
{
    const auto end = std::chrono::steady_clock::now() + 3s;
    while (!f()) { assert(std::chrono::steady_clock::now() < end); std::this_thread::sleep_for(1ms); }
}
int main()
{
    // A blocked read does not block the caller; visible work wins and stale work is cancelled.
    {
        std::promise<void> started, release;
        auto go = release.get_future().share();
        std::vector<std::string> loads; std::mutex mutex;
        Cache c([&](const std::string& path) {
            { std::lock_guard<std::mutex> lock(mutex); loads.push_back(path); }
            if (path == "slow") { started.set_value(); go.wait(); }
            return image();
        }, 256);
        assert(!c.get("slow")); started.get_future().wait();
        auto ui = std::async(std::launch::async, [&] {
            for (int i = 0; i < 1000; ++i) assert(!c.get("slow"));
            c.window({"old-visible", "old-ahead"}, 1);
            c.window({"new-visible", "new-ahead"}, 1);
            c.get("detail");
        });
        assert(ui.wait_for(250ms) == std::future_status::ready); ui.get();
        release.set_value();
        until([&] { return c.bytes() == 256; });
        c.stop();
        assert((loads == std::vector<std::string>{"slow", "new-visible", "detail", "new-ahead"}));
        assert(c.get("after-stop") == nullptr);
    }
    // Exact memory cap, LRU reuse, deduplication and failed-read backoff.
    {
        std::map<std::string,int> loads; std::mutex mutex;
        Cache c([&](const std::string& path) -> Cache::Ptr {
            { std::lock_guard<std::mutex> lock(mutex); ++loads[path]; }
            if (path == "missing") return {};
            if (path == "huge") return image(192);
            return image();
        }, 128, 50ms);
        c.get("a"); until([&] { return c.get("a") != nullptr; });
        for (int i = 0; i < 100; ++i) assert(c.get("a"));
        c.get("b"); until([&] { return c.get("b") != nullptr; });
        assert(c.bytes() == 128); assert(c.get("a"));
        c.get("c"); until([&] { return c.get("c") != nullptr; });
        assert(c.bytes() == 128); assert(c.get("a"));
        c.get("missing");
        until([&] { std::lock_guard<std::mutex> lock(mutex); return loads["missing"] == 1; });
        for (int i = 0; i < 100; ++i) assert(!c.get("missing"));
        std::this_thread::sleep_for(60ms); c.get("missing");
        until([&] { std::lock_guard<std::mutex> lock(mutex); return loads["missing"] == 2; });
        c.get("huge");
        until([&] { std::lock_guard<std::mutex> lock(mutex); return loads["huge"] == 1; });
        c.stop(); assert(c.bytes() <= 128);
        assert(loads["a"] == 1 && loads["b"] == 1 && loads["c"] == 1);
        assert(loads["missing"] == 2 && loads["huge"] == 1);
    }
    // Stop joins an in-flight read; no task survives the owning frontend.
    {
        std::promise<void> started, release; auto go = release.get_future().share();
        Cache c([&](const std::string&) { started.set_value(); go.wait(); return image(); });
        c.get("slow"); started.get_future().wait();
        auto stopped = std::async(std::launch::async, [&] { c.stop(); });
        assert(stopped.wait_for(10ms) == std::future_status::timeout);
        release.set_value(); assert(stopped.wait_for(1s) == std::future_status::ready);
        stopped.get(); assert(c.bytes() == 0);
    }
    std::cout << "PASS: nonblocking UI, priority, cancellation, dedup, LRU, memory cap, backoff, shutdown\n";
}
