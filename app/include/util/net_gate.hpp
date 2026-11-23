#pragma once

#include <functional>

namespace util
{
// Quick connectivity check. Always true off-Switch.
bool isOnline();

// Blocks until online (polls nifm). Shows a persistent non-cancelable popup
// while offline; returns false if shouldAbort() becomes true.
bool waitForConnection(const std::function<bool()>& shouldAbort = nullptr);
} // namespace util
