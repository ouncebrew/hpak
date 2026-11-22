#include "util/icon_loader.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <sys/stat.h>

#include <borealis.hpp>
#include "Utils.hpp"

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace util
{

static bool fileExists(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

struct IconLoader
{
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<IconLoadRequest> queue;
    std::set<std::string> queuedPaths;
    std::atomic<bool> running{false};

#if defined(__SWITCH__)
    Thread thread{};
    bool threadCreated = false;
#else
    std::thread stdThread;
#endif

    void start()
    {
        bool expected = false;
        if (!running.compare_exchange_strong(expected, true))
            return;

#if defined(__SWITCH__)
        // Create a dedicated thread; try to pin to core 1 to offload main/UI
        Result rc = threadCreate(&thread, &IconLoader::threadEntry, this, nullptr, 0x20000, 0x2B, 1);
        if (R_FAILED(rc))
        {
            // Fallback: let system choose core
            rc = threadCreate(&thread, &IconLoader::threadEntry, this, nullptr, 0x20000, 0x2B, -2);
        }
        if (R_SUCCEEDED(rc))
        {
            threadCreated = true;
            threadStart(&thread);
        }
        else
        {
            running.store(false);
            brls::Logger::error("IconLoader: failed to create thread: {:#x}", rc);
        }
#else
        stdThread = std::thread([this]() { this->loop(); });
#endif
    }

#if defined(__SWITCH__)
    static void threadEntry(void* arg)
    {
        auto* self = static_cast<IconLoader*>(arg);
        self->loop();
    }
#endif

    void loop()
    {
        while (running.load())
        {
            IconLoadRequest req;
            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait_for(lock, std::chrono::milliseconds(200), [this]() { return !queue.empty() || !running.load(); });
                if (!running.load() && queue.empty())
                    break;
                if (queue.empty())
                    continue;
                req = std::move(queue.front());
                queue.pop_front();
                queuedPaths.erase(req.cachePath);
            }

            bool ok = false;
            bool alive = !req.isAlive || req.isAlive();
            if (alive)
            {
                if (fileExists(req.cachePath))
                {
                    ok = true;
                }
                else
                {
                    std::string dir = req.cachePath.substr(0, req.cachePath.find_last_of('/'));
                    mkpath(dir);
                    ok = downloadFileToDisk(req.url, req.cachePath);
                }
            }

            if (req.onDone)
            {
                auto cb = req.onDone;
                brls::sync([cb, ok]() { cb(ok); });
            }
        }
    }

    void enqueue(IconLoadRequest req)
    {
        start();
        if (!running.load())
            return;

        std::lock_guard<std::mutex> lock(mtx);
        if (!req.cachePath.empty() && queuedPaths.count(req.cachePath))
            return;
        if (queue.size() > 64)
            return; // prevent unbounded queue on very large lists
        if (!req.cachePath.empty())
            queuedPaths.insert(req.cachePath);
        queue.push_back(std::move(req));
        cv.notify_one();
    }
};

static IconLoader& getLoader()
{
    static IconLoader loader;
    return loader;
}

void enqueueIconLoad(IconLoadRequest req)
{
    getLoader().enqueue(std::move(req));
}

} // namespace util
