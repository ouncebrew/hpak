#include "store/store.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>

#include "Utils.hpp"

static std::string toLowerStr(const std::string& in)
{
    std::string out = in;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

StoreManager& StoreManager::instance()
{
    static StoreManager mgr;
    return mgr;
}

void StoreManager::init()
{
    std::lock_guard<std::mutex> lock(mutex);
    if (initialized)
        return;

    // Initialize networking (curl/sockets) as needed by libget
    init_networking();

    // Default to the main 4TU Switch repo; users can manage repos later.
    // performInitialLoad=false so we can control loading and surface errors.
    get = std::make_unique<Get>(configPath, "https://switch.cdn.fortheusers.org/repo", false, "get");
    initialized = true;
}

std::string StoreManager::getLastError() const
{
    return lastError;
}

PackageSnapshot StoreManager::snapshotFrom(const Package& pkg)
{
    PackageSnapshot s;
    s.name         = pkg.getPackageName();
    s.title        = pkg.getTitle();
    s.author       = pkg.getAuthor();
    s.version      = pkg.getVersion();
    s.short_desc   = pkg.getShortDescription();
    s.long_desc    = pkg.getLongDescription();
    s.category     = pkg.getCategory();
    s.updated      = pkg.getUpdatedAt();
    s.license      = pkg.getLicense();
    s.url          = pkg.url;
    s.status       = pkg.getStatus();
    s.downloads    = pkg.getDownloadCount();
    s.download_size = pkg.getDownloadSize();
    s.extracted_size = pkg.getExtractedSize();
    s.screens      = pkg.getScreenshotCount();
    s.icon_url     = pkg.getIconUrl();
    return s;
}

std::shared_ptr<Package> StoreManager::findPackageLocked(const std::string& name)
{
    if (!get)
        return nullptr;
    return get->findPackage(name);
}

void StoreManager::refresh(std::function<void(bool success, const std::string& error)> onDone)
{
    if (loading)
    {
        if (onDone)
            brls::sync([onDone]() { onDone(false, "Already loading"); });
        return;
    }

    loading = true;
    brls::async([this, onDone]() {
        std::string err;
        bool ok = true;
        try
        {
            init();
            std::lock_guard<std::mutex> lock(mutex);
            if (get)
            {
                get->update();
            }
            else
            {
                ok = false;
                err = "Store not initialized";
            }
        }
        catch (const std::exception& e)
        {
            ok = false;
            err = e.what();
        }
        catch (...)
        {
            ok = false;
            err = "Unknown error while refreshing";
        }

        loading = false;
        lastError = err;

        if (onDone)
        {
            brls::sync([onDone, ok, err]() { onDone(ok, err); });
        }
    });
}

std::vector<PackageSnapshot> StoreManager::getAllPackages()
{
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<PackageSnapshot> out;
    if (!get)
        return out;
    auto pkgs = get->getPackages();
    out.reserve(pkgs.size());
    for (auto& p : pkgs)
    {
        if (p)
            out.push_back(snapshotFrom(*p));
    }
    std::sort(out.begin(), out.end(), [](const PackageSnapshot& a, const PackageSnapshot& b) {
        std::string ta = a.title.empty() ? a.name : a.title;
        std::string tb = b.title.empty() ? b.name : b.title;
        return toLowerStr(ta) < toLowerStr(tb);
    });
    return out;
}

std::vector<PackageSnapshot> StoreManager::getInstalledPackages()
{
    auto all = getAllPackages();
    std::vector<PackageSnapshot> out;
    for (auto& p : all)
        if (p.installed() || p.hasUpdate())
            out.push_back(p);
    return out;
}

std::vector<PackageSnapshot> StoreManager::getUpdatablePackages()
{
    auto all = getAllPackages();
    std::vector<PackageSnapshot> out;
    for (auto& p : all)
        if (p.hasUpdate())
            out.push_back(p);
    return out;
}

std::vector<PackageSnapshot> StoreManager::searchPackages(const std::string& query)
{
    if (query.empty())
        return getAllPackages();

    std::string q = toLowerStr(query);
    auto all = getAllPackages();
    std::vector<PackageSnapshot> out;
    for (auto& p : all)
    {
        std::string hay = toLowerStr(p.title + " " + p.name + " " + p.author + " " + p.short_desc + " " + p.category);
        if (hay.find(q) != std::string::npos)
            out.push_back(p);
    }
    return out;
}

PackageSnapshot StoreManager::getPackage(const std::string& name)
{
    std::lock_guard<std::mutex> lock(mutex);
    if (!get)
        return {};
    auto p = findPackageLocked(name);
    if (!p)
        return {};
    return snapshotFrom(*p);
}

void StoreManager::installPackage(const std::string& name, std::function<void(bool success, const std::string& error)> onDone)
{
    brls::async([this, name, onDone]() {
        bool ok = false;
        std::string err;
        try
        {
            init();
            std::lock_guard<std::mutex> lock(mutex);
            auto pkg = findPackageLocked(name);
            if (!pkg)
            {
                err = "Package not found";
            }
            else
            {
                int ret = get->install(*pkg);
                ok = (ret == 1);
                if (!ok)
                    err = "Install failed";
            }
        }
        catch (const std::exception& e)
        {
            err = e.what();
            ok = false;
        }
        catch (...)
        {
            err = "Unknown error during install";
            ok = false;
        }

        if (onDone)
            brls::sync([onDone, ok, err]() { onDone(ok, err); });
    });
}

void StoreManager::removePackage(const std::string& name, std::function<void(bool success, const std::string& error)> onDone)
{
    brls::async([this, name, onDone]() {
        bool ok = false;
        std::string err;
        try
        {
            init();
            std::lock_guard<std::mutex> lock(mutex);
            auto pkg = findPackageLocked(name);
            if (!pkg)
            {
                err = "Package not found";
            }
            else
            {
                int ret = get->remove(*pkg);
                ok = (ret == 1);
                if (!ok)
                    err = "Remove failed";
            }
        }
        catch (const std::exception& e)
        {
            err = e.what();
            ok = false;
        }
        catch (...)
        {
            err = "Unknown error during remove";
            ok = false;
        }

        if (onDone)
            brls::sync([onDone, ok, err]() { onDone(ok, err); });
    });
}
