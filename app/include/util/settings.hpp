#pragma once

#include <mutex>
#include <string>

struct HpSettings
{
    bool addDefaultRepos = true; // ensure Ouncebrew + Community sources
    bool mouseMode = true;       // Joy-Con 2 mouse mode
    bool fileLogging = false;    // log to sdmc:/hpak.txt (applies on next launch)

    static HpSettings& instance();

    void load();
    void save();

  private:
    HpSettings() = default;
    std::string path() const;
    std::mutex mutex;
};
