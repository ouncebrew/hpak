#include "store/store.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <dirent.h>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "Utils.hpp"
#include "ZipUtil.hpp"
#include "rapidjson/document.h"
#include "util/net_gate.hpp"
#include "util/settings.hpp"
#include <borealis/extern/nlohmann/json.hpp>

static std::string toLowerStr(const std::string& in)
{
    std::string out = in;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

static bool fileExistsLocal(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0;
}

// Delete a file or directory tree. Only acts inside sdmc:/ and never on the
// root itself, so malformed deletion entries can't wipe the card.
static void removePathRecursive(std::string path)
{
    while (!path.empty() && path.back() == '/')
        path.pop_back();
    if (path.rfind("sdmc:/", 0) != 0 || path.size() <= 6)
        return;
    if (path.find("..") != std::string::npos)
        return;

    struct stat st{};
    if (stat(path.c_str(), &st) != 0)
    {
        std::remove(path.c_str());
        return;
    }
    if (!S_ISDIR(st.st_mode))
    {
        std::remove(path.c_str());
        return;
    }
    DIR* dir = opendir(path.c_str());
    if (!dir)
    {
        std::remove(path.c_str());
        return;
    }
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr)
    {
        std::string n = ent->d_name;
        if (n == "." || n == "..")
            continue;
        removePathRecursive(path + "/" + n);
    }
    closedir(dir);
    rmdir(path.c_str());
}

static std::string readFileToString(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

struct DirectPackageMeta
{
    std::string title;
    std::string author;
    std::string version;
    std::string description;
    std::string details;
    std::string license;
    std::string changelog;
    std::vector<std::string> requiredDeletion;
    std::vector<std::string> userDeletion;
};

static std::vector<std::string> parseStringList(const nlohmann::json& j, const char* key)
{
    std::vector<std::string> out;
    auto it = j.find(key);
    if (it == j.end())
        return out;
    if (it->is_string())
        out.push_back(it->get<std::string>());
    else if (it->is_array())
    {
        for (auto& e : *it)
            if (e.is_string())
                out.push_back(e.get<std::string>());
    }
    return out;
}

static DirectPackageMeta parseDirectPackageJson(const std::string& content)
{
    DirectPackageMeta meta;
    auto j = nlohmann::json::parse(content, nullptr, false);
    if (j.is_discarded() || !j.is_object())
        return meta;

    auto getStr = [&](const char* key) -> std::string {
        if (j.contains(key) && j[key].is_string())
            return j[key].get<std::string>();
        return "";
    };

    meta.title = getStr("title");
    if (meta.title.empty()) meta.title = getStr("name");
    meta.author = getStr("author");
    meta.version = getStr("version");
    meta.description = getStr("description");
    if (meta.description.empty()) meta.description = getStr("short_desc");
    meta.details = getStr("details");
    if (meta.details.empty()) meta.details = getStr("long_desc");
    meta.license = getStr("license");
    meta.changelog = getStr("changelog");
    meta.requiredDeletion = parseStringList(j, "requiredDeletion");
    meta.userDeletion = parseStringList(j, "userDeletion");
    return meta;
}

static std::string sanitizeDirectVersion(std::string ver)
{
    std::string vl = toLowerStr(ver);
    if (ver.empty() || vl == "not found" || vl == "n/a" || vl == "unknown")
        return "1.0.0";
    return ver;
}

std::string StoreManager::repoCacheSlug(const std::string& url)
{
    std::string s = url;
    // Strip scheme
    size_t pos = s.find("://");
    if (pos != std::string::npos) s = s.substr(pos + 3);
    // Strip trailing slashes
    while (!s.empty() && s.back() == '/') s.pop_back();

    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s)
    {
        if (std::isalnum(c)) out.push_back((char)std::tolower(c));
        else out.push_back('_');
    }
    // Collapse multiple underscores
    std::string collapsed;
    bool lastUnderscore = false;
    for (char c : out)
    {
        if (c == '_')
        {
            if (!lastUnderscore) collapsed.push_back(c);
            lastUnderscore = true;
        }
        else
        {
            collapsed.push_back(c);
            lastUnderscore = false;
        }
    }
    // Trim underscores
    size_t start = collapsed.find_first_not_of('_');
    size_t end = collapsed.find_last_not_of('_');
    if (start == std::string::npos) return "repo";
    return collapsed.substr(start, end - start + 1);
}

std::vector<DefaultRepo> StoreManager::getDefaultRepos()
{
    std::vector<DefaultRepo> out;
    FILE* f = fopen("romfs:/util/defaultRepos.json", "rb");
    if (f)
    {
        std::string content;
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
            content.append(buf, n);
        fclose(f);

        auto j = nlohmann::json::parse(content, nullptr, false);
        if (j.is_object() && j.contains("repos") && j["repos"].is_array())
        {
            for (auto& e : j["repos"])
            {
                if (e.is_object() && e.contains("url") && e["url"].is_string())
                {
                    DefaultRepo d;
                    d.url = e["url"].get<std::string>();
                    if (e.contains("name") && e["name"].is_string())
                        d.name = e["name"].get<std::string>();
                    if (!d.url.empty())
                        out.push_back(std::move(d));
                }
            }
        }
    }
    if (out.empty())
    {
        // Fallback if romfs file is missing/unreadable (e.g. desktop builds).
        out.push_back({"Ouncebrew", "https://ouncebrew.github.io"});
    }
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

    // Default to the first bundled repo; users can manage repos later.
    // performInitialLoad=false so we can control loading and surface errors.
    std::string firstDefault = "https://ouncebrew.github.io";
    {
        auto defaults = StoreManager::getDefaultRepos();
        if (!defaults.empty() && !defaults[0].url.empty())
            firstDefault = defaults[0].url;
    }
    get = std::make_unique<Get>(configPath, firstDefault, false, "get");

    if (get)
    {
        bool wantDefaults = HpSettings::instance().addDefaultRepos;

        auto findRepo = [&](const std::string& url) -> std::shared_ptr<Repo> {
            for (auto& r : get->getRepos())
                if (r && r->getUrl() == url)
                    return r;
            return nullptr;
        };

        if (wantDefaults)
        {
            // Ensure all bundled sources exist; re-add any that are missing.
            std::unordered_map<std::string, std::string> toAdd;
            for (auto& d : StoreManager::getDefaultRepos())
            {
                if (!d.url.empty() && !findRepo(d.url))
                    toAdd[d.url] = "get";
            }
            if (!toAdd.empty())
            {
                std::unordered_set<std::string> toRemove;
                get->addAndRemoveReposByURL(toAdd, toRemove);
                for (auto& d : StoreManager::getDefaultRepos())
                {
                    auto r = findRepo(d.url);
                    if (r && !d.name.empty() &&
                        (r->getName() == "Default Repo" || r->getName().find('/') != std::string::npos))
                        r->setName(d.name);
                }
                get->saveRepos();
            }
        }
        else
        {
            // Defaults disabled: drop bundled sources if present (manual adds stay).
            std::unordered_map<std::string, std::string> toAdd;
            std::unordered_set<std::string> toRemove;
            for (auto& d : StoreManager::getDefaultRepos())
            {
                if (!d.url.empty() && findRepo(d.url))
                    toRemove.insert(d.url);
            }
            if (!toRemove.empty())
                get->addAndRemoveReposByURL(toAdd, toRemove);
        }
    }

    initialized = true;
}

void StoreManager::shutdown()
{
    shuttingDown.store(true);

    // Abort any in-flight libget/curl transfers
    networking_callback = [](void*, double) -> int { return 1; };
    libget_status_callback = [](int, int, int) -> int { return 1; };
}

void StoreManager::teardown()
{
    std::lock_guard<std::mutex> lock(mutex);
    get.reset();
    initialized = false;
    loading = false;
    deinit_networking();
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
    s.changelog    = pkg.getChangelog();
    s.dependencies = pkg.getDependencies();
    s.requiredDeletion = pkg.getRequiredDeletion();
    s.userDeletion = pkg.getUserDeletion();
    if (auto repo = pkg.getRepo()) { s.repo_name = repo->getName(); s.repo_url = repo->getUrl(); }
    s.url          = pkg.url;
    s.file         = pkg.file;
    s.directUrl    = pkg.directUrl;
    s.path         = pkg.path;
    s.status       = pkg.getStatus();
    s.downloads    = pkg.getDownloadCount();
    s.download_size = pkg.getDownloadSize();
    s.extracted_size = pkg.getExtractedSize();
    s.screens      = pkg.getScreenshotCount();
    s.icon_url     = pkg.getIconUrl();

    // Direct download package: override status/metadata from local file + package.json
    if (!s.file.empty())
    {
        std::string directBase = !s.directUrl.empty() ? s.directUrl : s.url;
        while (!directBase.empty() && directBase.back() == '/') directBase.pop_back();
        if (!directBase.empty())
            s.icon_url = directBase + "/icon.png";

        std::string lowerFile = toLowerStr(s.file);
        bool isZip = lowerFile.size() > 4 && lowerFile.rfind(".zip") == lowerFile.size() - 4;
        std::string installDir = s.path.empty() ? "sdmc:/switch/" : s.path;
        while (!installDir.empty() && installDir.back() != '/') installDir.push_back('/');
        std::string installedPath = isZip ? "" : (installDir + s.file);

        std::string localPath = configPath + "direct/" + s.name + ".version";
        std::string remotePath = configPath + "direct/" + s.name + ".remote";
        auto localMeta = parseDirectPackageJson(readFileToString(localPath));
        auto remoteMeta = parseDirectPackageJson(readFileToString(remotePath));

        bool repoHasFullMeta = !s.title.empty() && !s.author.empty() && !s.version.empty() &&
                               !s.short_desc.empty() && !s.long_desc.empty() && !s.license.empty() &&
                               !s.changelog.empty();

        std::string localVer = sanitizeDirectVersion(localMeta.version.empty() ? s.version : localMeta.version);
        std::string remoteVer = sanitizeDirectVersion(repoHasFullMeta ? s.version : (remoteMeta.version.empty() ? s.version : remoteMeta.version));

        if (!repoHasFullMeta)
        {
            auto applyMeta = [&](const DirectPackageMeta& m) {
                if (!m.title.empty()) s.title = m.title;
                if (!m.author.empty()) s.author = m.author;
                if (!m.description.empty()) s.short_desc = m.description;
                if (!m.details.empty()) s.long_desc = m.details;
                if (!m.license.empty()) s.license = m.license;
                if (!m.changelog.empty()) s.changelog = m.changelog;
                for (auto& p : m.requiredDeletion)
                {
                    if (std::find(s.requiredDeletion.begin(), s.requiredDeletion.end(), p) == s.requiredDeletion.end())
                        s.requiredDeletion.push_back(p);
                }
                for (auto& p : m.userDeletion)
                {
                    if (std::find(s.userDeletion.begin(), s.userDeletion.end(), p) == s.userDeletion.end())
                        s.userDeletion.push_back(p);
                }
            };

            auto hasMeta = [](const DirectPackageMeta& m) {
                return !m.title.empty() || !m.author.empty() || !m.version.empty() ||
                       !m.description.empty() || !m.details.empty() || !m.license.empty() || !m.changelog.empty();
            };

            // Prefer remote metadata (latest release), fall back to local
            if (hasMeta(localMeta))
                applyMeta(localMeta);
            if (hasMeta(remoteMeta))
                applyMeta(remoteMeta);
        }

        bool isInstalled = isZip ? fileExistsLocal(localPath) : (!installedPath.empty() && fileExistsLocal(installedPath));
        if (isInstalled)
        {
            s.status = INSTALLED;
            s.version = localVer;
            // Only a strictly newer remote version is an update; anything
            // else means we are up to date (or ahead — no downgrade offered).
            if (compareVersions(remoteVer, localVer) > 0)
            {
                s.status = UPDATE;
                s.version = remoteVer;
            }
        }
        else
        {
            s.status = GET;
            s.version = remoteVer;
        }
    }

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

        // Gate: wait for connectivity with a persistent popup instead of
        // hanging in network calls with no Wi-Fi/Ethernet.
        if (!util::waitForConnection([this]() { return shuttingDown.load(); }))
        {
            ok = false;
            err = "No internet connection";
        }
        else
        {
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
        }

        // Fix up generic "Default Repo" names from remote repo.json
        std::vector<std::pair<std::string, std::string>> reposToName; // url, name
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (get)
            {
                for (auto& r : get->getRepos())
                {
                    if (r && r->getName() == "Default Repo")
                        reposToName.push_back({r->getUrl(), ""});
                }
            }
        }
        for (auto& rn : reposToName)
        {
            if (shuttingDown.load()) break;
            std::string base = rn.first;
            while (!base.empty() && base.back() == '/') base.pop_back();
            std::string repoJson;
            if (downloadFileToMemory(base + "/repo.json", &repoJson))
            {
                rapidjson::Document doc;
                doc.Parse(repoJson.c_str());
                if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("name") && doc["name"].IsString())
                    rn.second = doc["name"].GetString();
            }
            if (rn.second.empty())
            {
                size_t hostStart = rn.first.find("://");
                std::string display = (hostStart != std::string::npos) ? rn.first.substr(hostStart + 3) : rn.first;
                while (!display.empty() && display.back() == '/') display.pop_back();
                rn.second = display;
            }
        }
        if (!reposToName.empty())
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (get)
            {
                for (auto& rn : reposToName)
                {
                    if (rn.second.empty()) continue;
                    for (auto& r : get->getRepos())
                    {
                        if (r && r->getUrl() == rn.first && r->getName() == "Default Repo")
                        {
                            r->setName(rn.second);
                            break;
                        }
                    }
                }
                get->saveRepos();
            }
        }

        // Fetch remote package.json metadata for direct-download packages
        // Skip if repo.json already provides all details (avoids rate limits)
        std::vector<std::pair<std::string, std::string>> directPkgs; // name, baseUrl
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (get)
            {
                for (auto& p : get->getPackages())
                {
                    if (p && !p->file.empty())
                    {
                        bool hasFullRepoMeta = !p->getTitle().empty() && !p->getAuthor().empty() &&
                                               !p->getVersion().empty() && !p->getShortDescription().empty() &&
                                               !p->getLongDescription().empty() && !p->getLicense().empty() &&
                                               !p->getChangelog().empty();
                        if (hasFullRepoMeta)
                            continue;
                        std::string base = !p->directUrl.empty() ? p->directUrl : p->url;
                        while (!base.empty() && base.back() == '/') base.pop_back();
                        directPkgs.push_back({p->getPackageName(), base});
                    }
                }
            }
        }
        for (auto& dp : directPkgs)
        {
            if (shuttingDown.load()) break;
            std::string content;
            if (downloadFileToMemory(dp.second + "/package.json", &content))
            {
                std::string dir = configPath + "direct";
                mkpath(dir);
                std::ofstream ofs(dir + "/" + dp.first + ".remote", std::ios::binary);
                if (ofs) ofs << content;
            }
        }

        // Download repo icons.zip bundles (compatible cache prefill)
        std::vector<std::pair<std::string, std::string>> repoIconZips; // slug, baseUrl
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (get)
            {
                for (auto& r : get->getRepos())
                {
                    if (!r) continue;
                    std::string base = r->getUrl();
                    while (!base.empty() && base.back() == '/') base.pop_back();
                    if (base.empty()) continue;
                    repoIconZips.push_back({StoreManager::repoCacheSlug(base), base});
                }
            }
        }
        for (auto& rz : repoIconZips)
        {
            if (shuttingDown.load()) break;
            std::string tmpDir = configPath + "tmp";
            mkpath(tmpDir);
            std::string tmpZip = tmpDir + "/icons.zip";
            if (!downloadFileToDisk(rz.second + "/icons.zip", tmpZip))
                continue;

            UnZip zip(tmpZip);
            if (!zip.IsValid())
            {
                zip.Close();
                std::remove(tmpZip.c_str());
                continue;
            }

            auto mapping = zip.GetPathToFilePosMapping();
            for (auto& entry : mapping)
            {
                const std::string& name = entry.first;
                std::string lower = toLowerStr(name);
                std::string pkgName, ext;
                if (lower.size() > 10 && lower.rfind("/icon.png") == lower.size() - 9)
                {
                    pkgName = name.substr(0, name.size() - 9);
                    ext = ".png";
                }
                else if (lower.size() > 10 && lower.rfind("/icon.jpg") == lower.size() - 9)
                {
                    pkgName = name.substr(0, name.size() - 9);
                    ext = ".jpg";
                }
                else if (lower.size() > 11 && lower.rfind("/icon.jpeg") == lower.size() - 10)
                {
                    pkgName = name.substr(0, name.size() - 10);
                    ext = ".jpeg";
                }
                else
                    continue;

                while (!pkgName.empty() && (pkgName.front() == '/' || pkgName.front() == '\\')) pkgName.erase(pkgName.begin());
                while (!pkgName.empty() && (pkgName.back() == '/' || pkgName.back() == '\\')) pkgName.pop_back();
                if (pkgName.empty()) continue;

                std::string outDir = configPath + "packages/" + rz.first + "/" + pkgName;
                mkpath(outDir);
                for (const char* oldExt : {".png", ".jpg", ".jpeg"})
                {
                    std::string oldPath = outDir + "/icon" + oldExt;
                    if (oldPath != outDir + "/icon" + ext)
                        std::remove(oldPath.c_str());
                }
                std::string outPath = outDir + "/icon" + ext;
                zip.Extract(outPath, entry.second);
            }

            zip.Close();
            std::remove(tmpZip.c_str());
        }

        // Refresh the last-known-good cache while holding the lock briefly.
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (get)
            {
                cachedPackages = getAllPackagesLocked();
                cachedRepos = getReposLocked();
            }
        }

        loading = false;
        lastError = err;

        if (onDone)
        {
            brls::sync([onDone, ok, err]() { onDone(ok, err); });
        }
    });
}

std::vector<PackageSnapshot> StoreManager::getAllPackagesLocked()
{
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

std::vector<PackageSnapshot> StoreManager::getAllPackages()
{
    // Never block the UI thread on an in-flight refresh: fall back to cache.
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() && get)
        cachedPackages = getAllPackagesLocked();
    return cachedPackages;
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
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() && get)
        cachedPackages = getAllPackagesLocked();
    for (auto& s : cachedPackages)
    {
        if (s.name == name)
        {
            brls::Logger::debug("hpak/pkg: '{}' status={} ver='{}' installed={} update={}",
                s.name, s.status, s.version, s.installed(), s.hasUpdate());
            return s;
        }
    }
    return {};
}

bool StoreManager::installDirectOne(const std::string& name, float p0, float p1, ProgressCallback onProgress, std::string& err)
{
    std::string directFile, directBase, pkgName, directPath;
    {
        std::lock_guard<std::mutex> lock(mutex);
        auto pkg = findPackageLocked(name);
        if (!pkg || pkg->file.empty())
        {
            err = "Package not found";
            brls::Logger::error("hpak/install: package '{}' not found", name);
            return false;
        }
        directFile = pkg->file;
        directBase = !pkg->directUrl.empty() ? pkg->directUrl : pkg->url;
        pkgName = pkg->getPackageName();
        directPath = pkg->path;
    }

    auto scale = [p0, p1](float p) { return p0 + p * (p1 - p0); };
    bool ok = false;

    while (!directBase.empty() && directBase.back() == '/') directBase.pop_back();
    std::string lowerFile = toLowerStr(directFile);
    bool isZip = lowerFile.size() > 4 && lowerFile.rfind(".zip") == lowerFile.size() - 4;
    std::string installDir = directPath.empty() ? "sdmc:/switch/" : directPath;
    while (!installDir.empty() && installDir.back() != '/') installDir.push_back('/');

    std::string fileUrl = directBase + "/" + directFile;
    std::string tmpDir = configPath + "tmp";
    mkpath(tmpDir);
    std::string destPath = isZip ? (tmpDir + "/" + directFile) : (installDir + directFile);
    if (!isZip) mkpath(installDir);

    struct DirectProgress { std::atomic<float> progress{0.0f}; };
    auto dstate = std::make_shared<DirectProgress>();
    networking_callback_data = dstate.get();
    libget_status_callback = nullptr;
    networking_callback = [](void* clientp, double p) -> int {
        auto* st = static_cast<DirectProgress*>(clientp);
        if (!st) return 0;
        float mapped = (float)p;
        float prev = st->progress.load();
        while (mapped > prev && !st->progress.compare_exchange_weak(prev, mapped)) {}
        return 0;
    };

    std::atomic<bool> dlDone{false};
    std::thread progressThread;
    if (onProgress)
    {
        progressThread = std::thread([dstate, onProgress, scale, &dlDone]() {
            int lastPct = -1;
            while (!dlDone.load())
            {
                int pct = (int)(dstate->progress.load() * 100);
                if (pct != lastPct)
                {
                    lastPct = pct;
                    float pval = scale(dstate->progress.load());
                    brls::sync([onProgress, pval]() { onProgress(pval, "Downloading..."); });
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            float pval = scale(1.0f);
            brls::sync([onProgress, pval]() { onProgress(pval, "Downloading..."); });
        });
    }

    ok = downloadFileToDisk(fileUrl, destPath);

    dlDone.store(true);
    if (progressThread.joinable()) progressThread.join();
    networking_callback = nullptr;
    networking_callback_data = nullptr;

    if (ok && isZip)
    {
        if (onProgress)
        {
            float pval = scale(1.0f);
            brls::sync([onProgress, pval]() { onProgress(pval, "Extracting..."); });
        }
        UnZip zip(destPath);
        if (zip.IsValid())
        {
            auto paths = zip.PathDump();
            std::string verDir = configPath + "direct";
            mkpath(verDir);
            std::ofstream mf(verDir + "/" + pkgName + ".files", std::ios::binary);
            for (auto& p : paths)
            {
                std::string clean = p;
                while (!clean.empty() && (clean.front() == '/' || clean.front() == '\\')) clean.erase(clean.begin());
                if (clean.empty()) continue;
                if (mf) mf << clean << "\n";
            }
            zip.ExtractAll("sdmc:/");
            zip.Close();
        }
        else
        {
            ok = false;
            err = "Failed to extract zip";
        }
        std::remove(destPath.c_str());
    }

    if (ok)
    {
        nlohmann::json j = nlohmann::json::object();
        bool hasFullRepoMeta = false;
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto pkg = findPackageLocked(name);
            if (pkg)
            {
                hasFullRepoMeta = !pkg->getTitle().empty() && !pkg->getAuthor().empty() &&
                                  !pkg->getVersion().empty() && !pkg->getShortDescription().empty() &&
                                  !pkg->getLongDescription().empty() && !pkg->getLicense().empty() &&
                                  !pkg->getChangelog().empty();
                if (hasFullRepoMeta)
                {
                    j["title"] = pkg->getTitle();
                    j["author"] = pkg->getAuthor();
                    j["version"] = pkg->getVersion();
                    j["description"] = pkg->getShortDescription();
                    j["details"] = pkg->getLongDescription();
                    j["license"] = pkg->getLicense();
                    j["changelog"] = pkg->getChangelog();
                    j["requiredDeletion"] = pkg->getRequiredDeletion();
                    j["userDeletion"] = pkg->getUserDeletion();
                }
            }
        }

        if (!hasFullRepoMeta)
        {
            std::string pkgContent;
            std::string pkgUrl = directBase + "/package.json";
            downloadFileToMemory(pkgUrl, &pkgContent);
            auto parsed = nlohmann::json::parse(pkgContent, nullptr, false);
            if (parsed.is_object())
                j = parsed;
        }

        auto meta = parseDirectPackageJson(j.dump());
        if (meta.version.empty())
        {
            std::lock_guard<std::mutex> lock(mutex);
            auto pkg = findPackageLocked(name);
            if (pkg) meta.version = pkg->getVersion();
        }
        meta.version = sanitizeDirectVersion(meta.version);
        j["version"] = meta.version;

        std::string stored = j.dump(4);
        std::string verDir = configPath + "direct";
        mkpath(verDir);
        std::string localVerPath = verDir + "/" + pkgName + ".version";
        std::string remoteVerPath = verDir + "/" + pkgName + ".remote";
        std::ofstream ofs(localVerPath, std::ios::binary);
        if (ofs) ofs << stored;
        std::ofstream ofs2(remoteVerPath, std::ios::binary);
        if (ofs2) ofs2 << stored;
    }
    else
    {
        if (err.empty()) err = "Download failed";
    }
    return ok;
}

bool StoreManager::installRegularOne(const std::string& name, float p0, float p1, ProgressCallback onProgress, std::string& err)
{
    std::shared_ptr<Package> pkg;
    {
        std::lock_guard<std::mutex> lock(mutex);
        pkg = findPackageLocked(name);
        if (!pkg)
        {
            err = "Package not found";
            brls::Logger::error("hpak/install: package '{}' not found", name);
            return false;
        }
    }

    brls::Logger::info("hpak/install: found '{}' status={} version='{}' url='{}'",
        pkg->getPackageName(), pkg->getStatus(), pkg->getVersion(), pkg->url);

    auto scale = [p0, p1](float p) { return p0 + p * (p1 - p0); };
    bool ok = false;

    // Wire libget global progress callbacks for this install
    struct ProgressState { std::atomic<float> progress{0.0f}; std::atomic<int> phase{0}; };
    auto state = std::make_shared<ProgressState>();

    networking_callback_data = state.get();
    libget_status_callback = [](int status, int, int) -> int {
        auto* st = static_cast<ProgressState*>(networking_callback_data);
        if (!st) return 0;
        st->phase.store(status);
        if (status == STATUS_DOWNLOADING) st->progress.store(std::max(st->progress.load(), 0.0f));
        else if (status == STATUS_ANALYZING) st->progress.store(std::max(st->progress.load(), 0.70f));
        else if (status == STATUS_INSTALLING) st->progress.store(std::max(st->progress.load(), 0.70f));
        return 0;
    };
    networking_callback = [](void* clientp, double p) -> int {
        auto* st = static_cast<ProgressState*>(clientp);
        if (!st) return 0;
        int phase = st->phase.load();
        float mapped = (float)p;
        if (phase == STATUS_DOWNLOADING) mapped = (float)(p * 0.70);
        else if (phase == STATUS_INSTALLING || phase == STATUS_ANALYZING) mapped = 0.70f + (float)(p * 0.30f);
        else mapped = (float)(p * 0.70);
        float prev = st->progress.load();
        while (mapped > prev && !st->progress.compare_exchange_weak(prev, mapped)) {}
        return 0;
    };

    // Periodic UI progress updates while install runs
    std::atomic<bool> installDone{false};
    std::thread progressThread;
    if (onProgress)
    {
        progressThread = std::thread([state, onProgress, scale, &installDone]() {
            int lastPct = -1;
            int lastPhase = -1;
            while (!installDone.load())
            {
                float prog = state->progress.load();
                int phase = state->phase.load();
                int pct = (int)(prog * 100);
                if (pct != lastPct || phase != lastPhase)
                {
                    lastPct = pct; lastPhase = phase;
                    std::string phaseStr = "Working...";
                    if (phase == STATUS_DOWNLOADING) phaseStr = "Downloading...";
                    else if (phase == STATUS_ANALYZING) phaseStr = "Analyzing...";
                    else if (phase == STATUS_INSTALLING) phaseStr = "Extracting...";
                    float pval = scale(prog);
                    brls::sync([onProgress, pval, phaseStr]() { onProgress(pval, phaseStr); });
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
            float prog = scale(state->progress.load());
            int phase = state->phase.load();
            std::string phaseStr = (phase == STATUS_INSTALLING) ? "Extracting..." : "Finishing...";
            brls::sync([onProgress, prog, phaseStr]() { onProgress(prog, phaseStr); });
        });
    }

    int ret;
    {
        // Serialize against concurrent refresh/update: libget's package list
        // must not be read (UI) or rewritten (refresh) mid-install. UI readers
        // use try_lock and fall back to cache, so this never freezes the UI.
        std::lock_guard<std::mutex> lock(mutex);
        ret = get->installSingle(*pkg);
    }
    installDone.store(true);
    if (progressThread.joinable()) progressThread.join();

    networking_callback = nullptr;
    libget_status_callback = nullptr;
    networking_callback_data = nullptr;

    ok = (ret == 1);
    brls::Logger::info("hpak/install: installSingle('{}') returned {} (ok={})", name, ret, ok);
    if (!ok)
        err = "Install failed";
    return ok;
}

void StoreManager::installPackage(const std::string& name, std::function<void(bool success, const std::string& error)> onDone, ProgressCallback onProgress)
{
    brls::async([this, name, onDone, onProgress]() {
        bool ok = false;
        std::string err;
        try
        {
            brls::Logger::info("hpak/install: requested '{}'", name);
            init();

            // Resolve the full dependency closure (dependencies first, root last).
            // Version constraints and cycles are validated here; errors abort.
            std::vector<std::string> order;
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!findPackageLocked(name))
                {
                    err = "Package not found";
                    brls::Logger::error("hpak/install: package '{}' not found", name);
                }
                else
                {
                    std::vector<std::string> o;
                    std::unordered_set<std::string> visiting, visited;
                    std::string derr;
                    if (!get->resolveDependencies(name, o, visiting, visited, derr))
                        err = derr;
                    else
                        order = std::move(o);
                }
            }
            if (err.empty() && order.empty())
                err = "Package not found";

            if (err.empty())
            {
                int total = (int)order.size();
                for (int i = 0; i < total; i++)
                {
                    const std::string& item = order[i];
                    bool isRoot = (item == name);
                    float p0 = (float)i / total, p1 = (float)(i + 1) / total;

                    bool isDirect = false;
                    int status = 0;
                    {
                        std::lock_guard<std::mutex> lock(mutex);
                        auto pkg = findPackageLocked(item);
                        if (!pkg)
                        {
                            err = "Package '" + item + "' not found";
                            break;
                        }
                        isDirect = !pkg->file.empty();
                        status = pkg->getStatus();
                    }

                    if (isDirect)
                    {
                        if (!isRoot)
                        {
                            auto snap = getPackage(item);
                            if (snap.name.empty())
                            {
                                err = "Package '" + item + "' not found";
                                break;
                            }
                            if (snap.installed() && !snap.hasUpdate())
                            {
                                brls::Logger::info("hpak/install: dependency '{}' already satisfied", item);
                                continue;
                            }
                            brls::Logger::info("hpak/install: installing dependency '{}'", item);
                        }
                        if (!installDirectOne(item, p0, p1, onProgress, err))
                            break;
                    }
                    else
                    {
                        if (!isRoot && (status == INSTALLED || status == LOCAL))
                        {
                            brls::Logger::info("hpak/install: dependency '{}' already satisfied", item);
                            continue;
                        }
                        if (!isRoot)
                            brls::Logger::info("hpak/install: installing dependency '{}'", item);
                        if (!installRegularOne(item, p0, p1, onProgress, err))
                            break;
                    }
                }
                ok = err.empty();
            }

            if (ok && onProgress)
                brls::sync([onProgress]() { onProgress(1.0f, "Done"); });

            // Refresh the last-known-good cache (statuses changed).
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (get)
                    cachedPackages = getAllPackagesLocked();
            }
        }
        catch (const std::exception& e)
        {
            err = e.what();
            ok = false;
            brls::Logger::error("hpak/install: exception for '{}': {}", name, err);
        }
        catch (...)
        {
            err = "Unknown error during install";
            ok = false;
            brls::Logger::error("hpak/install: unknown exception for '{}'", name);
        }

        if (onDone)
            brls::sync([onDone, ok, err]() { onDone(ok, err); });
    });
}
std::vector<RepoSnapshot> StoreManager::getReposLocked()
{
    std::vector<RepoSnapshot> out;
    if (!get)
        return out;
    for (auto& r : get->getRepos())
    {
        if (!r) continue;
        RepoSnapshot s;
        s.name = r->getName();
        s.url = r->getUrl();
        s.enabled = r->isEnabled();
        s.loaded = r->isLoaded();
        out.push_back(std::move(s));
    }
    return out;
}

std::vector<RepoSnapshot> StoreManager::getRepos()
{
    // Never block the UI thread on an in-flight refresh: fall back to cache.
    std::unique_lock<std::mutex> lock(mutex, std::try_to_lock);
    if (lock.owns_lock() && get)
        cachedRepos = getReposLocked();
    return cachedRepos;
}

void StoreManager::addRepo(const std::string& url, std::function<void(bool success, const std::string& error)> onDone)
{
    brls::async([this, url, onDone]() {
        bool ok = false;
        std::string err;
        try
        {
            init();
            std::string cleanUrl = url;
            while (!cleanUrl.empty() && (cleanUrl.back() == '/' || cleanUrl.back() == ' ' || cleanUrl.back() == '\r' || cleanUrl.back() == '\n'))
                cleanUrl.pop_back();
            size_t start = cleanUrl.find_first_not_of(" \t\r\n");
            if (start != std::string::npos) cleanUrl = cleanUrl.substr(start);
            if (cleanUrl.empty())
            {
                err = "URL is empty";
            }
            else
            {
                // Try to fetch repo name from remote repo.json
                std::string repoName;
                std::string repoJson;
                std::string jsonUrl = cleanUrl + "/repo.json";
                if (downloadFileToMemory(jsonUrl, &repoJson))
                {
                    rapidjson::Document doc;
                    doc.Parse(repoJson.c_str());
                    if (!doc.HasParseError() && doc.IsObject() && doc.HasMember("name") && doc["name"].IsString())
                        repoName = doc["name"].GetString();
                }
                if (repoName.empty())
                {
                    size_t hostStart = cleanUrl.find("://");
                    std::string display = (hostStart != std::string::npos) ? cleanUrl.substr(hostStart + 3) : cleanUrl;
                    while (!display.empty() && display.back() == '/')
                        display.pop_back();
                    repoName = display.empty() ? cleanUrl : display;
                }

                std::lock_guard<std::mutex> lock(mutex);
                bool exists = false;
                for (auto& r : get->getRepos())
                {
                    if (r && r->getUrl() == cleanUrl)
                    {
                        exists = true;
                        break;
                    }
                }
                if (exists)
                {
                    err = "Source already exists";
                }
                else
                {
                    std::unordered_map<std::string, std::string> toAdd;
                    toAdd[cleanUrl] = "get";
                    std::unordered_set<std::string> toRemove;
                    get->addAndRemoveReposByURL(toAdd, toRemove);
                    // Apply name from remote repo.json if available
                    if (!repoName.empty())
                    {
                        for (auto& r : get->getRepos())
                        {
                            if (r && r->getUrl() == cleanUrl)
                            {
                                r->setName(repoName);
                                break;
                            }
                        }
                        get->saveRepos();
                    }
                    ok = true;
                }
            }
        }
        catch (const std::exception& e)
        {
            err = e.what();
        }
        catch (...)
        {
            err = "Unknown error adding source";
        }
        if (onDone)
            brls::sync([onDone, ok, err]() { onDone(ok, err); });
    });
}

void StoreManager::removePackage(const std::string& name, std::function<void(bool success, const std::string& error)> onDone, ProgressCallback /*onProgress*/, bool removeUserData)
{
    brls::async([this, name, onDone, removeUserData]() {
        bool ok = false;
        std::string err;
        try
        {
            init();

            // Block removal when an installed package depends on this one.
            // Flat scan (no recursion), so circular dependencies can't loop here;
            // install-time cycles are already rejected by resolveDependencies.
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (!findPackageLocked(name))
                {
                    err = "Package not found";
                }
                else
                {
                    std::vector<std::string> blockers;
                    for (auto& p : get->getPackages())
                    {
                        if (!p || p->getPackageName() == name)
                            continue;
                        bool needsIt = false;
                        for (auto& dep : p->getDependencies())
                        {
                            if (dep.name == name)
                            {
                                needsIt = true;
                                break;
                            }
                        }
                        if (!needsIt)
                            continue;
                        bool installed = false;
                        if (!p->file.empty())
                            installed = snapshotFrom(*p).installed();
                        else
                        {
                            int st = p->getStatus();
                            installed = (st == INSTALLED || st == LOCAL || st == UPDATE);
                        }
                        if (installed)
                            blockers.push_back(p->getPackageName());
                    }
                    if (!blockers.empty())
                    {
                        err = "Cannot remove '" + name + "': required by ";
                        for (size_t i = 0; i < blockers.size(); i++)
                        {
                            if (i > 0) err += ", ";
                            err += "'" + blockers[i] + "'";
                        }
                    }
                }
            }
            if (!err.empty())
            {
                ok = false;
            }
            else
            {
            // Gather deletion lists: install-time marker first, live repo fields as fallback.
            std::vector<std::string> requiredDel, userDel;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto pkg = findPackageLocked(name);
                if (pkg && !pkg->file.empty())
                {
                    auto lm = parseDirectPackageJson(readFileToString(configPath + "direct/" + pkg->getPackageName() + ".version"));
                    requiredDel = lm.requiredDeletion;
                    userDel = lm.userDeletion;
                }
                if (pkg)
                {
                    for (auto& p : pkg->getRequiredDeletion())
                        if (std::find(requiredDel.begin(), requiredDel.end(), p) == requiredDel.end())
                            requiredDel.push_back(p);
                    for (auto& p : pkg->getUserDeletion())
                        if (std::find(userDel.begin(), userDel.end(), p) == userDel.end())
                            userDel.push_back(p);
                }
            }
            for (auto& p : requiredDel)
                removePathRecursive(p);
            if (removeUserData)
            {
                for (auto& p : userDel)
                    removePathRecursive(p);
            }

            std::string directFile, pkgName, directPath;
            bool isDirect = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto pkg = findPackageLocked(name);
                if (pkg && !pkg->file.empty())
                {
                    isDirect = true;
                    directFile = pkg->file;
                    pkgName = pkg->getPackageName();
                    directPath = pkg->path;
                }
            }

            if (isDirect)
            {
                std::string lowerFile = toLowerStr(directFile);
                bool isZip = lowerFile.size() > 4 && lowerFile.rfind(".zip") == lowerFile.size() - 4;
                if (isZip)
                {
                    std::string manifestPath = configPath + "direct/" + pkgName + ".files";
                    std::ifstream mf(manifestPath);
                    std::string line;
                    while (std::getline(mf, line))
                    {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        while (!line.empty() && (line.front() == '/' || line.front() == '\\')) line.erase(line.begin());
                        if (line.empty()) continue;
                        std::remove(("sdmc:/" + line).c_str());
                    }
                    std::remove(manifestPath.c_str());
                }
                else
                {
                    std::string installDir = directPath.empty() ? "sdmc:/switch/" : directPath;
                    while (!installDir.empty() && installDir.back() != '/') installDir.push_back('/');
                    std::remove((installDir + directFile).c_str());
                }
                std::remove((configPath + "direct/" + pkgName + ".version").c_str());
                std::remove((configPath + "direct/" + pkgName + ".remote").c_str());
                ok = true;
            }
            else
            {
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
            } // end else: no installed dependents blocking removal

            // Refresh the last-known-good cache (statuses changed).
            {
                std::lock_guard<std::mutex> lock(mutex);
                if (get)
                    cachedPackages = getAllPackagesLocked();
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

std::vector<std::string> StoreManager::getUnusedDependencies(const std::string& removedName)
{
    std::vector<std::string> orphans;
    std::lock_guard<std::mutex> lock(mutex);
    if (!get)
        return orphans;

    // Transitive closure of the removed package (dependencies first, root last).
    // Fails on circular dependencies or missing packages -> no orphans reported.
    std::vector<std::string> order;
    std::unordered_set<std::string> visiting, visited;
    std::string err;
    if (!get->resolveDependencies(removedName, order, visiting, visited, err))
        return orphans;

    auto isInstalled = [this](const std::shared_ptr<Package>& p) {
        if (!p)
            return false;
        if (!p->file.empty())
            return snapshotFrom(*p).installed();
        int st = p->getStatus();
        return st == INSTALLED || st == LOCAL || st == UPDATE;
    };

    // Packages still required: every direct dependency of an installed package
    // other than the removed one.
    std::unordered_set<std::string> used;
    for (auto& p : get->getPackages())
    {
        if (!p || p->getPackageName() == removedName || !isInstalled(p))
            continue;
        for (auto& dep : p->getDependencies())
            used.insert(dep.name);
    }

    // Installed closure members that nothing installed still uses.
    // Reverse into dependents-first order for safe sequential removal.
    for (auto it = order.rbegin(); it != order.rend(); ++it)
    {
        if (*it == removedName)
            continue;
        if (used.find(*it) != used.end())
            continue;
        auto pkg = findPackageLocked(*it);
        if (pkg && isInstalled(pkg))
            orphans.push_back(*it);
    }
    return orphans;
}

void StoreManager::removeRepo(const std::string& url, std::function<void(bool success, const std::string& error)> onDone)
{
    brls::async([this, url, onDone]() {
        bool ok = false;
        std::string err;
        try
        {
            init();
            std::lock_guard<std::mutex> lock(mutex);
            std::unordered_map<std::string, std::string> toAdd;
            std::unordered_set<std::string> toRemove;
            toRemove.insert(url);
            get->addAndRemoveReposByURL(toAdd, toRemove);
            ok = true;
        }
        catch (const std::exception& e)
        {
            err = e.what();
        }
        catch (...)
        {
            err = "Unknown error removing source";
        }
        if (onDone)
            brls::sync([onDone, ok, err]() { onDone(ok, err); });
    });
}
