#include "view/source_cell.hpp"

#include <sys/stat.h>
#include "Utils.hpp"
#include "store/store.hpp"
#include "util/icon_loader.hpp"

static bool fileExists(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

static std::string sanitizeForPath(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
            out.push_back(c);
        else
            out.push_back('_');
    }
    return out;
}

SourceCell::SourceCell()
{
    this->inflateFromXMLRes("xml/cells/source_cell.xml");
}

SourceCell::~SourceCell()
{
    *alive = false;
}

void SourceCell::setRepo(const RepoSnapshot& r)
{
    this->repo = r;
    this->name->setText(r.name.empty() ? r.url : r.name);
    this->urlLabel->setText(r.url);

    this->icon->clear();
    this->icon->setVisibility(brls::Visibility::VISIBLE);

    std::string iconUrl = r.iconUrl();
    if (iconUrl.empty())
    {
        this->icon->setVisibility(brls::Visibility::GONE);
        return;
    }

    std::string repoSlug = StoreManager::repoCacheSlug(r.url);
    std::string cacheDir = "sdmc:/packages/.get/cache/" + repoSlug;
    std::string cachePath = cacheDir + "/icon.png";

    auto findCachedIcon = [&]() -> std::string {
        for (const char* ext : {".png", ".jpg", ".jpeg"})
        {
            std::string p = cacheDir + "/icon" + ext;
            if (fileExists(p)) return p;
        }
        return "";
    };

    std::string cached = findCachedIcon();
    if (!cached.empty())
    {
        this->icon->setImageFromFile(cached);
        return;
    }

    std::string url = iconUrl;
    std::string repoUrl = r.url;
    std::weak_ptr<bool> aliveWeak = alive;
    util::enqueueIconLoad({
        .name = repoUrl,
        .url = url,
        .cachePath = cachePath,
        .isAlive = [aliveWeak]() {
            auto aliveToken = aliveWeak.lock();
            return aliveToken && *aliveToken;
        },
        .onDone = [this, aliveWeak, repoUrl](bool ok, const std::string& resolvedPath) {
            auto aliveToken = aliveWeak.lock();
            if (!aliveToken || !*aliveToken)
                return;
            if (this->repo.url != repoUrl)
                return;
            if (ok && !resolvedPath.empty() && fileExists(resolvedPath))
                this->icon->setImageFromFile(resolvedPath);
            else
                this->icon->setVisibility(brls::Visibility::GONE);
        }
    });
}

SourceCell* SourceCell::create()
{
    return new SourceCell();
}
