#include "util/icon_decode.hpp"

#include "util/icon_loader.hpp"

// Private stb build: static linkage so it can never clash with nanovg's own
// copy at link time. Unused-function warnings are expected (we only decode
// via stbi_load) and silenced for this header.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#include <borealis/extern/nanovg/stb_image.h>
#pragma GCC diagnostic pop

#include <cstdio>
#include <list>
#include <mutex>
#include <unordered_map>

#include <borealis.hpp>
#include <borealis/core/cache_helper.hpp>

namespace util
{
namespace
{
// Cap decoded pixels in flight: icons are small (tens of KB), 128 entries is
// a few MB worst case. Evicted entries simply decode again on demand.
constexpr size_t kMaxDecodedIcons = 128;
// Sanity bound for a single icon; larger files are rejected, not cached.
constexpr int kMaxIconDim = 1024;

std::mutex g_mutex;
std::list<std::string> g_lru; // front = most recently used
struct CacheEntry
{
    std::list<std::string>::iterator it;
    DecodedIcon icon;
};
std::unordered_map<std::string, CacheEntry> g_cache;

void touchLocked(std::unordered_map<std::string, CacheEntry>::iterator it)
{
    g_lru.splice(g_lru.begin(), g_lru, it->second.it);
}
} // namespace

bool getDecodedIcon(const std::string& path, DecodedIcon& out)
{
    if (path.empty())
        return false;
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_cache.find(path);
    if (it == g_cache.end())
        return false;
    if (it->second.icon.width <= 0 || !it->second.icon.rgba || it->second.icon.rgba->empty())
        return false;
    touchLocked(it);
    out = it->second.icon;
    return true;
}

bool decodeIconNow(const std::string& path)
{
    if (path.empty())
        return false;
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_cache.find(path);
        if (it != g_cache.end())
        {
            touchLocked(it);
            return true;
        }
    }

    int w = 0, h = 0;
    unsigned char* px = stbi_load(path.c_str(), &w, &h, nullptr, 4);
    if (!px || w <= 0 || h <= 0 || w > kMaxIconDim || h > kMaxIconDim)
    {
        if (px)
            stbi_image_free(px);
        return false;
    }

    DecodedIcon icon;
    icon.width = w;
    icon.height = h;
    icon.rgba = std::make_shared<std::vector<unsigned char>>(px, px + (size_t)w * h * 4);
    stbi_image_free(px);

    {
        std::lock_guard<std::mutex> lock(g_mutex);
        auto it = g_cache.find(path);
        if (it != g_cache.end())
        {
            touchLocked(it);
            return true;
        }
        while (g_cache.size() >= kMaxDecodedIcons && !g_lru.empty())
        {
            g_cache.erase(g_lru.back());
            g_lru.pop_back();
        }
        g_lru.push_front(path);
        g_cache[path] = CacheEntry{g_lru.begin(), std::move(icon)};
    }
    return true;
}

void ensureDecodedIcon(const std::string& path, std::function<void(bool ok)> onDecoded)
{
    enqueueIconDecode(path, std::move(onDecoded));
}

void clearDecodedIcons()
{
    std::lock_guard<std::mutex> lock(g_mutex);
    g_cache.clear();
    g_lru.clear();
}

bool uploadDecodedIcon(brls::Image* view, const std::string& key, const DecodedIcon& icon)
{
    if (!view || key.empty() || icon.width <= 0 || icon.height <= 0 || !icon.rgba ||
        icon.rgba->size() < (size_t)icon.width * icon.height * 4)
        return false;
    NVGcontext* vg = brls::Application::getNVGContext();
    if (!vg)
        return false;

    // Mirror Image::setImageFromFile: the TextureCache owns the texture
    // lifetime (views must not delete it), entries deduped by key.
    view->setFreeTexture(false);
    int old = view->getTexture();
    if (old != 0)
        brls::TextureCache::instance().removeCache(old);
    int tex = brls::TextureCache::instance().getCache(key);
    if (tex <= 0)
    {
        // Icons use default (linear) interpolation like the cells' XML.
        tex = nvgCreateImageRGBA(vg, icon.width, icon.height, 0, icon.rgba->data());
        if (tex > 0)
            brls::TextureCache::instance().addCache(key, (size_t)tex);
    }
    if (tex <= 0)
        return false;
    view->innerSetImage(tex);
    return true;
}

} // namespace util
