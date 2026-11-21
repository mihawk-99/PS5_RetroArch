// Bounded, CPU-only image prefetch. Network/decoding never runs under the cache lock.
#ifndef PS5_SMB_IMAGE_CACHE_H
#define PS5_SMB_IMAGE_CACHE_H
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace SmbImages {
struct Image {
    size_t width = 0, height = 0;
    size_t sourceWidth = 0, sourceHeight = 0;
    std::vector<unsigned char> pixels; // ES-DE's premultiplied, bottom-up BGRA.
};

class Cache {
public:
    using Ptr = std::shared_ptr<const Image>;
    using Loader = std::function<Ptr(const std::string&)>;
    explicit Cache(Loader loader, size_t budget = 64 * 1024 * 1024,
                   std::chrono::milliseconds retry = std::chrono::seconds(5))
        : mLoader(std::move(loader)), mBudget(budget), mRetry(retry),
          mWorker([this] { run(); }) {}
    ~Cache() { stop(); }
    Cache(const Cache&) = delete;
    Cache& operator=(const Cache&) = delete;

    Ptr get(const std::string& path, unsigned priority = 0)
    {
        if (path.empty()) return {};
        std::lock_guard<std::mutex> lock(mMutex);
        if (mStop) return {};
        auto& e = mEntries[path];
        e.used = ++mClock;
        if (e.image) return e.image;
        enqueue(path, e, priority);
        trimEntries();
        mEvent.notify_one();
        return {};
    }

    // Ordered by importance: visible/selected first, then upcoming, then preceding.
    // Called by the visible grid, not by construction of all system views.
    void window(const std::vector<std::string>& paths, size_t visible)
    {
        std::lock_guard<std::mutex> lock(mMutex);
        if (mStop || (paths == mWindow && visible == mVisible)) return;
        mWindow = paths;
        mVisible = visible;
        mQueue.clear();
        for (auto it = mEntries.begin(); it != mEntries.end();) {
            if (it->second.queued) it = mEntries.erase(it);
            else ++it;
        }
        for (size_t i = 0; i < paths.size() && i < 128; ++i) {
            auto& e = mEntries[paths[i]];
            e.used = ++mClock;
            enqueue(paths[i], e, i < visible ? 0 : 1);
        }
        trimEntries();
        mEvent.notify_one();
    }

    size_t bytes() const { std::lock_guard<std::mutex> lock(mMutex); return mBytes; }
    size_t pending() const { std::lock_guard<std::mutex> lock(mMutex); return mQueue.size(); }
    void stop()
    {
        { std::lock_guard<std::mutex> lock(mMutex); mStop = true; mQueue.clear(); }
        mEvent.notify_one();
        if (mWorker.joinable()) mWorker.join();
    }

private:
    using Clock = std::chrono::steady_clock;
    struct Entry {
        Ptr image;
        bool queued = false, loading = false;
        unsigned priority = 1;
        size_t used = 0;
        Clock::time_point retry{};
    };
    void enqueue(const std::string& path, Entry& e, unsigned priority)
    {
        if (e.image || e.loading || Clock::now() < e.retry) return;
        if (e.queued) {
            e.priority = std::min(e.priority, priority);
            return;
        }
        if (mQueue.size() >= 128) return;
        e.queued = true; e.priority = priority;
        mQueue.push_back(path);
    }
    void trimEntries()
    {
        while (mEntries.size() > 256) {
            auto oldest = mEntries.end();
            for (auto it = mEntries.begin(); it != mEntries.end(); ++it)
                if (!it->second.loading && !it->second.queued &&
                    (oldest == mEntries.end() || it->second.used < oldest->second.used)) oldest = it;
            if (oldest == mEntries.end()) break;
            if (oldest->second.image) mBytes -= oldest->second.image->pixels.size();
            mEntries.erase(oldest);
        }
    }
    void run()
    {
        for (;;) {
            std::string path;
            {
                std::unique_lock<std::mutex> lock(mMutex);
                mEvent.wait(lock, [this] { return mStop || !mQueue.empty(); });
                if (mStop) return;
                auto next = std::min_element(mQueue.begin(), mQueue.end(), [this](const auto& a, const auto& b) {
                    return mEntries.at(a).priority < mEntries.at(b).priority;
                });
                path = *next; mQueue.erase(next);
                auto& e = mEntries.at(path); e.queued = false; e.loading = true;
            }
            Ptr result;
#if defined(__cpp_exceptions)
            try { result = mLoader(path); } catch (...) { result.reset(); }
#else
            result = mLoader(path);
#endif
            std::lock_guard<std::mutex> lock(mMutex);
            if (mStop) return;
            auto& e = mEntries.at(path); e.loading = false;
            if (!result || !result->width || !result->height ||
                result->pixels.size() > mBudget ||
                result->width > mBudget / 4 || result->height > mBudget / 4 / result->width ||
                result->pixels.size() != result->width * result->height * 4) {
                e.retry = Clock::now() + mRetry;
                continue;
            }
            while (mBytes + result->pixels.size() > mBudget) {
                auto oldest = mEntries.end();
                for (auto it = mEntries.begin(); it != mEntries.end(); ++it)
                    if (it->second.image &&
                        (oldest == mEntries.end() || it->second.used < oldest->second.used)) oldest = it;
                if (oldest == mEntries.end()) break;
                mBytes -= oldest->second.image->pixels.size();
                mEntries.erase(oldest);
            }
            e.image = std::move(result); e.used = ++mClock;
            mBytes += e.image->pixels.size();
            trimEntries();
        }
    }
    Loader mLoader;
    size_t mBudget, mBytes = 0, mClock = 0, mVisible = 0;
    std::chrono::milliseconds mRetry;
    mutable std::mutex mMutex;
    std::condition_variable mEvent;
    std::map<std::string, Entry> mEntries;
    std::vector<std::string> mQueue, mWindow;
    bool mStop = false;
    std::thread mWorker;
};
}
#endif
