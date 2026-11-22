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
    std::function<void(bool ok)> onDone; // called via brls::sync on UI thread
};

void enqueueIconLoad(IconLoadRequest req);
void shutdownIconLoader();
} // namespace util
