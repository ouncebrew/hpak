#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <borealis.hpp>

namespace util
{
// Decoded RGBA icon, ready for UI-thread GPU upload. Pixel data is
// reference-counted so copies are cheap.
struct DecodedIcon
{
    int width = 0;
    int height = 0;
    // width*height*4 bytes RGBA, or null when empty.
    std::shared_ptr<std::vector<unsigned char>> rgba;
};

// Returns true + fills out if decoded pixels for path are cached.
// Safe from any thread; cheap (no disk, no decode).
bool getDecodedIcon(const std::string& path, DecodedIcon& out);

// Worker threads only: load + decode path into the cache (LRU, capped).
// Returns false when the file is missing or undecodable.
// Never touches the GPU; safe from any thread.
bool decodeIconNow(const std::string& path);

// Queue a background decode; onDecoded(ok) runs via brls::sync on the UI
// thread. If already decoded, onDecoded(true) is invoked directly.
void ensureDecodedIcon(const std::string& path, std::function<void(bool ok)> onDecoded = nullptr);

// Drop all cached pixels (shutdown / memory pressure).
void clearDecodedIcons();

// UI thread only: upload decoded pixels, mirroring
// Image::setImageFromFile's TextureCache bookkeeping (cache-owned lifetime,
// deduped by key). Returns false when the upload fails (caller should fall
// back to setImageFromFile or hide the view).
bool uploadDecodedIcon(brls::Image* view, const std::string& key, const DecodedIcon& icon);
} // namespace util
