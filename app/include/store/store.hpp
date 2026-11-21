#pragma once

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
    std::string license;
    std::string repo_name;
    std::string repo_url;
    std::string icon_url;
    std::string url;
    int status = 0;
    int downloads = 0;
    int download_size = 0;
    int extracted_size = 0;
    int screens = 0;

    bool installed() const { return status == INSTALLED || status == LOCAL; }
    bool hasUpdate() const { return status == UPDATE; }
};

class StoreManager
{
  public:
    static StoreManager& instance();

    void init();
    bool isInitialized() const { return initialized; }
    bool isLoading() const { return loading; }
    std::string getLastError() const;

    void refresh(std::function<void(bool success, const std::string& error)> onDone = nullptr);

    std::vector<PackageSnapshot> getAllPackages();
    std::vector<PackageSnapshot> getInstalledPackages();
    std::vector<PackageSnapshot> getUpdatablePackages();
    std::vector<PackageSnapshot> searchPackages(const std::string& query);
    PackageSnapshot getPackage(const std::string& name);

    using ProgressCallback = std::function<void(float progress, const std::string& phase)>;

    void installPackage(const std::string& name, std::function<void(bool success, const std::string& error)> onDone = nullptr, ProgressCallback onProgress = nullptr);
    void removePackage(const std::string& name, std::function<void(bool success, const std::string& error)> onDone = nullptr, ProgressCallback onProgress = nullptr);

    std::string getConfigPath() const { return configPath; }

  private:
    StoreManager() = default;

    PackageSnapshot snapshotFrom(const Package& pkg);
    std::shared_ptr<Package> findPackageLocked(const std::string& name);

    std::unique_ptr<Get> get;
    std::mutex mutex;
    std::string configPath = "sdmc:/switch/hpak/.get/";
    bool initialized = false;
    bool loading = false;
    std::string lastError;
};
