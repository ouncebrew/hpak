#pragma once

#include <functional>
#include <string>

namespace util
{
// Conditional-GET file downloader with an on-disk cache.
//
// - `destPath` holds the last good body; metadata (ETag/Last-Modified) is
//   stored alongside at `destPath + ".meta"`.
// - The body is written atomically (tmp file + rename), so a failed request
//   never corrupts a previously cached file.
// - If the server answers 304 Not Modified, returns true with
//   `*outNotModified = true` and leaves `destPath` untouched.
// - `progressCb` (0.0..1.0, download fraction) is invoked on the calling
//   thread; may be nullptr.
// - Each call uses a private curl handle, so this is safe from any thread
//   (including several concurrent workers) and independent of libget's
//   shared handle. Must be called after networking is initialized
//   (StoreManager::init() -> init_networking()).
// - Returns false on network errors or unexpected HTTP status. On failure the
//   previous cache file (if any) is left in place for stale fallback.
bool downloadCached(const std::string& url, const std::string& destPath,
    bool* outNotModified = nullptr,
    const std::function<void(double progress)>& progressCb = nullptr);
} // namespace util
