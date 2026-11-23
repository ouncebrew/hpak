#include "store/store.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#include "Utils.hpp"
#include "ZipUtil.hpp"
#include "rapidjson/document.h"
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

static std::string readFileToString(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

static std::string trimWhitespace(std::string s)
{
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
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
};

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

    // Default to the Ouncebrew repo; users can manage repos later.
    // performInitialLoad=false so we can control loading and surface errors.
    get = std::make_unique<Get>(configPath, "https://ouncebrew.github.io", false, "get");

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
    s.url          = pkg.url;
    s.file         = pkg.file;
    s.directUrl    = pkg.directUrl;
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

        std::string installedPath = "sdmc:/switch/" + s.file;
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

        if (fileExistsLocal(installedPath))
        {
            s.status = INSTALLED;
            s.version = localVer;
            if (remoteVer != localVer)
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
                std::string outPath = outDir + "/icon" + ext;
                zip.Extract(outPath, entry.second);
            }

            zip.Close();
            std::remove(tmpZip.c_str());
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
    auto snap = snapshotFrom(*p);
    brls::Logger::debug("hpak/pkg: '{}' status={} ver='{}' installed={} update={}", 
        snap.name, snap.status, snap.version, snap.installed(), snap.hasUpdate());
    return snap;
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

            // Check if this is a direct-download package
            std::string directFile, directBase, pkgName;
            bool isDirect = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto pkg = findPackageLocked(name);
                if (pkg && !pkg->file.empty())
                {
                    isDirect = true;
                    directFile = pkg->file;
                    directBase = !pkg->directUrl.empty() ? pkg->directUrl : pkg->url;
                    pkgName = pkg->getPackageName();
                }
            }

            if (isDirect)
            {
                while (!directBase.empty() && directBase.back() == '/') directBase.pop_back();
                std::string fileUrl = directBase + "/" + directFile;
                std::string destPath = "sdmc:/switch/" + directFile;
                mkpath("sdmc:/switch");

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
                    auto progressCb = onProgress;
                    progressThread = std::thread([dstate, progressCb, &dlDone]() {
                        int lastPct = -1;
                        while (!dlDone.load())
                        {
                            int pct = (int)(dstate->progress.load() * 100);
                            if (pct != lastPct)
                            {
                                lastPct = pct;
                                float pval = dstate->progress.load();
                                brls::sync([progressCb, pval]() { progressCb(pval, "Downloading..."); });
                            }
                            std::this_thread::sleep_for(std::chrono::milliseconds(100));
                        }
                        brls::sync([progressCb]() { progressCb(1.0f, "Downloading..."); });
                    });
                }

                ok = downloadFileToDisk(fileUrl, destPath);

                dlDone.store(true);
                if (progressThread.joinable()) progressThread.join();
                networking_callback = nullptr;
                networking_callback_data = nullptr;

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

                    if (onProgress)
                        brls::sync([onProgress]() { onProgress(1.0f, "Done"); });
                }
                else
                {
                    err = "Download failed";
                }
            }
            else
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto pkg = findPackageLocked(name);
                if (!pkg)
                {
                    err = "Package not found";
                    brls::Logger::error("hpak/install: package '{}' not found", name);
                }
                else
                {
                    brls::Logger::info("hpak/install: found '{}' status={} version='{}' url='{}'",
                        pkg->getPackageName(), pkg->getStatus(), pkg->getVersion(), pkg->url);

                    // Wire libget global progress callbacks for this install
                    struct ProgressState { std::atomic<float> progress{0.0f}; std::atomic<int> phase{0}; };
                    auto state = std::make_shared<ProgressState>();
                    auto progressCb = onProgress;

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
                    if (progressCb)
                    {
                        progressThread = std::thread([state, progressCb, &installDone]() {
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
                                    float pval = prog;
                                    brls::sync([progressCb, pval, phaseStr]() { progressCb(pval, phaseStr); });
                                }
                                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                            }
                            float prog = state->progress.load();
                            int phase = state->phase.load();
                            std::string phaseStr = (phase == STATUS_INSTALLING) ? "Extracting..." : "Finishing...";
                            brls::sync([progressCb, prog, phaseStr]() { progressCb(prog, phaseStr); });
                        });
                    }

                    int ret = get->install(*pkg);
                    installDone.store(true);
                    if (progressThread.joinable()) progressThread.join();

                    networking_callback = nullptr;
                    libget_status_callback = nullptr;
                    networking_callback_data = nullptr;

                    ok = (ret == 1);
                    brls::Logger::info("hpak/install: get->install('{}') returned {} (ok={})", name, ret, ok);
                    if (!ok)
                        err = "Install failed";
                }
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
std::vector<RepoSnapshot> StoreManager::getRepos()
{
    std::lock_guard<std::mutex> lock(mutex);
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

void StoreManager::removePackage(const std::string& name, std::function<void(bool success, const std::string& error)> onDone, ProgressCallback /*onProgress*/)
{
    brls::async([this, name, onDone]() {
        bool ok = false;
        std::string err;
        try
        {
            init();

            std::string directFile, pkgName;
            bool isDirect = false;
            {
                std::lock_guard<std::mutex> lock(mutex);
                auto pkg = findPackageLocked(name);
                if (pkg && !pkg->file.empty())
                {
                    isDirect = true;
                    directFile = pkg->file;
                    pkgName = pkg->getPackageName();
                }
            }

            if (isDirect)
            {
                std::remove(("sdmc:/switch/" + directFile).c_str());
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
