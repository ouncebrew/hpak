#include "view/app_cell.hpp"

#include <sys/stat.h>
#include "Utils.hpp"
#include "util/icon_decode.hpp"
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

    std::string repoSlug = StoreManager::repoCacheSlug(pkg.repo_url);
    std::string cacheDir = "sdmc:/packages/.get/packages/" + repoSlug + "/" + pkg.name;
    std::string cachePath = cacheDir + "/icon.png";

    // In-memory icon path cache: repeat binds skip the disk stat() calls.
    // (Decoded pixels live in util::DecodedIcon cache filled by worker
    // threads; uploaded textures are cached by borealis' TextureCache. The
    // loader thread invalidates the path entry on fresh downloads.)
    std::string cached = util::resolveCachedIconPath(cacheDir);
    if (!cached.empty())
    {
        // Fast path: pre-decoded on a worker thread, so the UI thread only
        // uploads pixels to the GPU (no file I/O, no PNG decode here).
        util::DecodedIcon decoded;
        if (util::getDecodedIcon(cached, decoded) && util::uploadDecodedIcon(this->icon, cached, decoded))
            return;
        // Rare miss (icon landed after the refresh pre-decode): decode off
        // the UI thread and pop the icon in when ready.
        std::string name = pkg.name;
        std::weak_ptr<bool> aliveWeak = alive;
        util::ensureDecodedIcon(cached, [this, aliveWeak, name, cached](bool ok) {
            auto aliveToken = aliveWeak.lock();
            if (!aliveToken || !*aliveToken)
                return;
            if (this->package.name != name)
                return;
            util::DecodedIcon di;
            if (ok && util::getDecodedIcon(cached, di) && util::uploadDecodedIcon(this->icon, cached, di))
                return;
            this->icon->setImageFromFile(cached); // last resort (also warms TextureCache)
        });
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
        .onDone = [this, aliveWeak, name](bool ok, const std::string& resolvedPath) {
            auto aliveToken = aliveWeak.lock();
            if (!aliveToken || !*aliveToken)
                return;
            if (this->package.name != name)
                return;
            if (ok && !resolvedPath.empty())
            {
                // The loader thread decodes right after downloading, so this
                // is normally a hit: the UI thread only uploads pixels.
                util::DecodedIcon di;
                if (!util::getDecodedIcon(resolvedPath, di) ||
                    !util::uploadDecodedIcon(this->icon, resolvedPath, di))
                {
                    if (fileExists(resolvedPath))
                        this->icon->setImageFromFile(resolvedPath);
                    else
                        this->icon->setVisibility(brls::Visibility::GONE);
                }
                return;
            }
            this->icon->setVisibility(brls::Visibility::GONE);
        }
    });
}

AppCell* AppCell::create()
{
    return new AppCell();
}
