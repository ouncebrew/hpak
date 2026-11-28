#include "util/icon_loader.hpp"
#include "util/icon_decode.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>
#include <vector>
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

    curl_easy_setopt(c, CURLOPT_CAINFO, tlsCaBundlePath().c_str());
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

namespace
{
// Resolved icon path per cache dir ("" = checked, nothing on disk).
// Guarded by its own mutex; only ever holds tiny strings.
std::mutex g_iconPathMutex;
std::unordered_map<std::string, std::string> g_iconPaths;
} // namespace

std::string resolveCachedIconPath(const std::string& cacheDir)
{
    if (cacheDir.empty())
        return "";
    {
        std::lock_guard<std::mutex> lock(g_iconPathMutex);
        auto it = g_iconPaths.find(cacheDir);
        if (it != g_iconPaths.end())
            return it->second;
    }
    std::string resolved;
    for (const char* ext : {".png", ".jpg", ".jpeg"})
    {
        std::string p = cacheDir + "/icon" + ext;
        if (fileExists(p))
        {
            resolved = p;
            break;
        }
    }
    {
        std::lock_guard<std::mutex> lock(g_iconPathMutex);
        g_iconPaths[cacheDir] = resolved;
    }
    return resolved;
}

void invalidateIconPath(const std::string& cacheDir)
{
    if (cacheDir.empty())
        return;
    std::lock_guard<std::mutex> lock(g_iconPathMutex);
    g_iconPaths.erase(cacheDir);
}

void clearIconPathCache()
{
    std::lock_guard<std::mutex> lock(g_iconPathMutex);
    g_iconPaths.clear();
}

struct IconLoader
{
    std::mutex mtx;
    std::condition_variable cv;
    std::deque<IconLoadRequest> queue;
    std::set<std::string> queuedPaths;
    // Background PNG/JPEG decodes (file I/O + stb, never on the UI thread).
    struct DecodeJob
    {
        std::string path;
        std::function<void(bool ok)> onDone;
    };
    std::deque<DecodeJob> decodeQueue;
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
            DecodeJob dec;
            bool isDecode = false;
            {
                std::unique_lock<std::mutex> lock(mtx);
                cv.wait_for(lock, std::chrono::milliseconds(200), [this]() {
                    return !queue.empty() || !decodeQueue.empty() || !running.load();
                });
                if (!running.load() && queue.empty() && decodeQueue.empty())
                    break;
                if (!queue.empty())
                {
                    req = std::move(queue.front());
                    queue.pop_front();
                    queuedPaths.erase(req.cachePath);
                }
                else if (!decodeQueue.empty())
                {
                    dec = std::move(decodeQueue.front());
                    decodeQueue.pop_front();
                    isDecode = true;
                }
                else
                    continue;
            }

            if (isDecode)
            {
                // Pure decode job: file I/O + stb stay on this thread; the UI
                // thread only uploads the pixels later.
                bool ok = decodeIconNow(dec.path);
                if (dec.onDone)
                {
                    auto cb = dec.onDone;
                    brls::sync([cb, ok]() { cb(ok); });
                }
                continue;
            }

            bool ok = false;
            std::string resolvedPath = req.cachePath;
            bool alive = !req.isAlive || req.isAlive();
            if (alive)
            {
                auto tryExtensions = [](const std::string& base) {
                    std::vector<std::string> out;
                    out.push_back(base);
                    auto replaceExt = [&](const char* ext) {
                        size_t dot = base.find_last_of('.');
                        if (dot != std::string::npos)
                        {
                            std::string v = base.substr(0, dot) + ext;
                            if (v != base) out.push_back(v);
                        }
                    };
                    replaceExt(".jpg");
                    replaceExt(".jpeg");
                    replaceExt(".png");
                    return out;
                };

                // Check cached variants
                for (const auto& p : tryExtensions(req.cachePath))
                {
                    if (fileExists(p))
                    {
                        ok = true;
                        resolvedPath = p;
                        break;
                    }
                }

                if (!ok)
                {
                    std::string dir = req.cachePath.substr(0, req.cachePath.find_last_of('/'));
                    mkpath(dir);

                    // Try URL as-is, then common image extensions
                    std::vector<std::pair<std::string, std::string>> attempts;
                    attempts.push_back({req.url, req.cachePath});
                    auto replaceUrlExt = [&](const char* ext) {
                        size_t dot = req.url.find_last_of('.');
                        size_t slash = req.url.find_last_of('/');
                        if (dot != std::string::npos && (slash == std::string::npos || dot > slash))
                        {
                            std::string u = req.url.substr(0, dot) + ext;
                            std::string c = req.cachePath;
                            size_t cdot = c.find_last_of('.');
                            if (cdot != std::string::npos) c = c.substr(0, cdot) + ext;
                            if (u != req.url) attempts.push_back({u, c});
                        }
                    };
                    replaceUrlExt(".jpg");
                    replaceUrlExt(".jpeg");
                    replaceUrlExt(".png");

                    for (const auto& a : attempts)
                    {
                        if (downloadIconToDisk(a.first, a.second))
                        {
                            ok = true;
                            resolvedPath = a.second;
                            break;
                        }
                    }
                }
            }

            if (req.onDone)
            {
                // A fresh download may have resolved a previously cached
                // negative (or added a new extension variant): drop the entry
                // so the next bind re-stats the directory.
                if (ok)
                {
                    size_t slash = req.cachePath.find_last_of('/');
                    if (slash != std::string::npos)
                        invalidateIconPath(req.cachePath.substr(0, slash));
                    // Decode here on the loader thread so the UI thread only
                    // uploads pixels in onDone, never file I/O + PNG decode.
                    if (!resolvedPath.empty())
                        decodeIconNow(resolvedPath);
                }
                auto cb = req.onDone;
                brls::sync([cb, ok, resolvedPath]() { cb(ok, resolvedPath); });
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

    void enqueueDecode(const std::string& path, std::function<void(bool ok)> onDone)
    {
        if (!running.load())
            start();
        if (!running.load())
        {
            if (onDone)
                brls::sync([onDone]() { onDone(false); });
            return;
        }

        std::lock_guard<std::mutex> lock(mtx);
        decodeQueue.push_back(DecodeJob{path, std::move(onDone)});
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

void enqueueIconDecode(const std::string& path, std::function<void(bool ok)> onDecoded)
{
    if (path.empty())
    {
        if (onDecoded)
            brls::sync([onDecoded]() { onDecoded(false); });
        return;
    }
    DecodedIcon probe;
    if (getDecodedIcon(path, probe))
    {
        // Already decoded: invoke directly (callers are on the UI thread and
        // just upload from here).
        if (onDecoded)
            onDecoded(true);
        return;
    }
    getLoader().enqueueDecode(path, std::move(onDecoded));
}

void shutdownIconLoader()
{
    getLoader().shutdown();
}

} // namespace util
