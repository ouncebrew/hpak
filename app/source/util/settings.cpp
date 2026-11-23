#include "util/settings.hpp"

#include <cstdio>
#include <borealis/extern/nlohmann/json.hpp>
#include "Utils.hpp"

HpSettings& HpSettings::instance()
{
    static HpSettings s;
    return s;
}

std::string HpSettings::path() const
{
    return "sdmc:/packages/settings.json";
}

void HpSettings::load()
{
    std::lock_guard<std::mutex> lock(mutex);
    FILE* f = fopen(path().c_str(), "rb");
    if (!f)
        return;
    std::string content;
    char buf[512];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
        content.append(buf, n);
    fclose(f);

    auto j = nlohmann::json::parse(content, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return;
    if (j.contains("addDefaultRepos") && j["addDefaultRepos"].is_boolean())
        addDefaultRepos = j["addDefaultRepos"].get<bool>();
    if (j.contains("mouseMode") && j["mouseMode"].is_boolean())
        mouseMode = j["mouseMode"].get<bool>();
    if (j.contains("fileLogging") && j["fileLogging"].is_boolean())
        fileLogging = j["fileLogging"].get<bool>();
}

void HpSettings::save()
{
    std::lock_guard<std::mutex> lock(mutex);
    nlohmann::json j = nlohmann::json::object();
    j["addDefaultRepos"] = addDefaultRepos;
    j["mouseMode"] = mouseMode;
    j["fileLogging"] = fileLogging;
    std::string out = j.dump(4);
    mkpath("sdmc:/packages");
    FILE* f = fopen(path().c_str(), "wb");
    if (!f)
        return;
    fwrite(out.c_str(), 1, out.size(), f);
    fclose(f);
}
