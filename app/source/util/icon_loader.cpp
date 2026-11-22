#include "util/icon_loader.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <sys/stat.h>

#include <borealis.hpp>
#include <curl/curl.h>
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

static size_t iconWriteCallback(void* contents, size_t size, size_t nmemb, void* userp)
{
    auto* fp = static_cast<FILE*>(userp);
    return fwrite(contents, size, nmemb, fp);
}

static bool downloadIconToDisk(const std::string& url, const std::string& localPath)
{
    CURL* c = curl_easy_init();
    if (!c)
        return false;

    FILE* fp = fopen(localPath.c_str(), "wb");
    if (!fp)
    {
        curl_easy_cleanup(c);
        return false;
    }

#ifndef __SWITCH__
    curl_easy_setopt(c, CURLOPT_CAINFO, RAMFS "res/cacert.pem");
#else
    curl_easy_setopt(c, CURLOPT_CAINFO, "romfs:/util/cacert.pem");
#endif
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, iconWriteCallback);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, fp);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_USERAGENT, "hpak-icon/1.0");
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 15L);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 1L);

    CURLcode res = curl_easy_perform(c);
    fclose(fp);
    curl_easy_cleanup(c);

    if (res != CURLE_OK)
    {
        remove(localPath.c_str());
        return false;
    }
    return true;
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
                    ok = downloadIconToDisk(req.url, req.cachePath);
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
        if (!running.load())
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

    void shutdown()
    {
        bool expected = true;
        if (!running.compare_exchange_strong(expected, false))
            return;

        cv.notify_all();

#if defined(__SWITCH__)
        if (threadCreated)
        {
            threadWaitForExit(&thread);
            threadClose(&thread);
            threadCreated = false;
        }
#else
        if (stdThread.joinable())
            stdThread.join();
#endif
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

void shutdownIconLoader()
{
    getLoader().shutdown();
}

} // namespace util
