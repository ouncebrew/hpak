#include "view/app_cell.hpp"

#include <sys/stat.h>
#include "Utils.hpp"
#include "util/icon_loader.hpp"

static bool fileExists(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

AppCell::AppCell()
{
    this->inflateFromXMLRes("xml/cells/app_cell.xml");
}

AppCell::~AppCell()
{
    *alive = false;
}

void AppCell::setPackage(const PackageSnapshot& pkg)
{
    this->package = pkg;

    std::string displayTitle = pkg.title.empty() ? pkg.name : pkg.title;
    this->title->setText(displayTitle);
    this->version->setText(pkg.version.empty() ? "" : "v" + pkg.version);

    std::string sub = pkg.author.empty() ? "" : pkg.author;
    if (!pkg.short_desc.empty())
    {
        if (!sub.empty())
            sub += " - ";
        sub += pkg.short_desc;
    }
    if (sub.empty() && !pkg.category.empty())
        sub = pkg.category;
    this->subtitle->setText(sub);

    if (pkg.hasUpdate())
        this->status->setText("Update");
    else if (pkg.installed())
        this->status->setText("Installed");
    else
        this->status->setText("");

    // Icon: try local cache, then async download
    this->icon->clear();
    this->icon->setVisibility(brls::Visibility::VISIBLE);

    std::string cacheDir = "sdmc:/packages/.get/packages/" + pkg.name;
    std::string cachePath = cacheDir + "/icon.png";

    if (fileExists(cachePath))
    {
        this->icon->setImageFromFile(cachePath);
        return;
    }

    if (pkg.icon_url.empty())
    {
        this->icon->setVisibility(brls::Visibility::GONE);
        return;
    }

    // Async download icon on dedicated icon thread (core 1 on Switch)
    std::string url = pkg.icon_url;
    std::string name = pkg.name;
    std::weak_ptr<bool> aliveWeak = alive;
    util::enqueueIconLoad({
        .name = name,
        .url = url,
        .cachePath = cachePath,
        .isAlive = [aliveWeak]() {
            auto aliveToken = aliveWeak.lock();
            return aliveToken && *aliveToken;
        },
        .onDone = [this, aliveWeak, name, cachePath](bool ok) {
            auto aliveToken = aliveWeak.lock();
            if (!aliveToken || !*aliveToken)
                return;
            if (this->package.name != name)
                return;
            if (ok && fileExists(cachePath))
                this->icon->setImageFromFile(cachePath);
            else
                this->icon->setVisibility(brls::Visibility::GONE);
        }
    });
}

AppCell* AppCell::create()
{
    return new AppCell();
}
