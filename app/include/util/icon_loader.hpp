#pragma once

#include <functional>
#include <string>

namespace util
{
struct IconLoadRequest
{
    std::string name;
    std::string url;
    std::string cachePath;
    std::function<bool()> isAlive; // returns false if request is stale
    std::function<void(bool ok, const std::string& resolvedPath)> onDone; // called via brls::sync on UI thread
};

void enqueueIconLoad(IconLoadRequest req);
void shutdownIconLoader();

// Queue a background PNG/JPEG decode (runs on the loader thread, never the
// UI thread). onDecoded(ok) runs via brls::sync on the UI thread. If the
// image is already decoded, onDecoded(true) is invoked directly.
void enqueueIconDecode(const std::string& path, std::function<void(bool ok)> onDecoded = nullptr);

// In-memory cache of resolved on-disk icon paths (avoids repeated stat()
// calls when list cells rebind). Thread-safe; values are paths or "".
// Negatives are cached too, so callers MUST invalidate when an icon may have
// appeared/changed (after downloads, after a refresh's icon prefill).
std::string resolveCachedIconPath(const std::string& cacheDir);
void invalidateIconPath(const std::string& cacheDir);
void clearIconPathCache();
} // namespace util
