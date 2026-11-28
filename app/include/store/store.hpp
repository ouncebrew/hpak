#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include <borealis.hpp>
#include "Get.hpp"
#include "Package.hpp"
#include "repos/Repo.hpp"

struct PackageSnapshot
{
    std::string name;
    std::string title;
    std::string author;
    std::string version;
    std::string short_desc;
    std::string long_desc;
    std::string category;
    std::string updated;
    int updated_timestamp = 0;
    std::string license;
    std::string changelog;
    std::vector<PackageDependency> dependencies;
    std::vector<std::string> requiredDeletion;
    std::vector<std::string> userDeletion;
    std::string repo_name;
    std::string repo_url;
    std::string icon_url;
    std::string url;
    std::string file; // direct download file (e.g. dusklight.nro)
    std::string directUrl; // direct download base URL
    std::string path; // install path for non-zip direct downloads
    int status = 0;
    int downloads = 0;
    int download_size = 0;
    int extracted_size = 0;
    int screens = 0;

    bool installed() const { return status == INSTALLED || status == LOCAL; }
    bool hasUpdate() const { return status == UPDATE; }
};

struct RepoSnapshot
{
    std::string name;
    std::string url;
    bool enabled = true;
    bool loaded = false;
    std::string iconUrl() const
    {
        if (url.empty()) return "";
        std::string u = url;
        while (!u.empty() && u.back() == '/') u.pop_back();
        return u + "/icon.png";
    }
};

struct DefaultRepo
{
    std::string name;
    std::string url;
};

class StoreManager
{
  public:
    static StoreManager& instance();

    using ProgressCallback = std::function<void(float progress, const std::string& phase)>;

    void init();
    void ensureInitializedAsync(std::function<void()> onDone = nullptr);
    bool isInitialized() const { return initialized.load(); }
    bool isLoading() const { return loading.load(); }
    bool hasLoadedOnce() const { return loadedOnce.load(); }
    float getLoadProgress() const { return loadProgress.load(); }
    std::string getLoadPhase() const;
    std::string getLastError() const;

    void refresh(std::function<void(bool success, const std::string& error)> onDone = nullptr,
        ProgressCallback onProgress = nullptr);
    void refreshIfNeeded(std::function<void(bool success, const std::string& error)> onDone = nullptr,
        ProgressCallback onProgress = nullptr);

    std::vector<PackageSnapshot> getAllPackages();
    std::vector<PackageSnapshot> getInstalledPackages();
    std::vector<PackageSnapshot> getUpdatablePackages();
    std::vector<PackageSnapshot> searchPackages(const std::string& query);
    PackageSnapshot getPackage(const std::string& name);
    // Installed packages depending on `name` whose version constraint the
    // given new version would violate. Returns {dependent, constraint} pairs.
    std::vector<std::pair<std::string, std::string>> getDependencyViolations(const std::string& name, const std::string& newVersion);

    std::vector<RepoSnapshot> getRepos();
    void addRepo(const std::string& url, std::function<void(bool success, const std::string& error)> onDone = nullptr);
    void removeRepo(const std::string& url, std::function<void(bool success, const std::string& error)> onDone = nullptr);

    void installPackage(const std::string& name, std::function<void(bool success, const std::string& error)> onDone = nullptr, ProgressCallback onProgress = nullptr);
    void removePackage(const std::string& name, std::function<void(bool success, const std::string& error)> onDone = nullptr, ProgressCallback onProgress = nullptr, bool removeUserData = true);

    // Installed packages from the removed package's dependency closure that no
    // other installed package depends on (dependents-first order). Empty if the
    // closure can't be resolved (e.g. circular dependencies).
    std::vector<std::string> getUnusedDependencies(const std::string& removedName);

    std::string getConfigPath() const { return configPath; }
    void shutdown();
    void teardown();
    bool isShuttingDown() const { return shuttingDown.load(); }

    static std::string repoCacheSlug(const std::string& url);
    static std::vector<DefaultRepo> getDefaultRepos();

  private:
    StoreManager() = default;

    PackageSnapshot snapshotFrom(const Package& pkg);
    std::shared_ptr<Package> findPackageLocked(const std::string& name);
    std::vector<PackageSnapshot> getAllPackagesLocked();
    std::vector<RepoSnapshot> getReposLocked();

    // Install a single direct-download package; progress is reported scaled
    // into [p0, p1]. Returns true on success (err set otherwise).
    bool installDirectOne(const std::string& name, float p0, float p1, ProgressCallback onProgress, std::string& err);
    // Install a single regular (zip) package via libget; progress scaled into [p0, p1].
    bool installRegularOne(const std::string& name, float p0, float p1, ProgressCallback onProgress, std::string& err);
    // Extract icons from a repo's icons.zip into packages/<slug>/<pkg>/,
    // skipping icons already on disk (incremental prefill). Thread-safe
    // across repos (distinct slugs); called from refresh worker threads.
    void extractRepoIcons(const std::string& zipPath, const std::string& slug);

    std::unique_ptr<Get> get;
    std::mutex mutex;
    std::mutex initMutex;
    mutable std::mutex cacheMutex;
    std::mutex callbackMutex;
    struct PendingRefresh
    {
        std::function<void(bool success, const std::string& error)> onDone;
        ProgressCallback onProgress;
    };
    std::vector<PendingRefresh> pendingRefresh;
    void setLoadProgress(float p, const std::string& phase);
    void notifyProgress(float p, const std::string& phase);
    void finishRefresh(bool ok, const std::string& err);

    std::string configPath = "sdmc:/packages/.get/";
    std::atomic<bool> initialized{false};
    std::atomic<bool> loading{false};
    std::atomic<bool> loadedOnce{false};
    std::atomic<float> loadProgress{0.0f};
    std::string loadPhase;
    std::string lastError;
    std::atomic<bool> shuttingDown{false};
    // Last-known-good snapshots so UI threads never block on a refresh.
    std::vector<PackageSnapshot> cachedPackages;
    std::vector<RepoSnapshot> cachedRepos;
};
