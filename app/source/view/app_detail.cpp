#include "view/app_detail.hpp"

#include <fmt/format.h>
#include <sys/stat.h>
#include "util/icon_loader.hpp"

static bool fileExists(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

AppDetailView::AppDetailView(const PackageSnapshot& pkg) : package(pkg)
{
    this->inflateFromXMLRes("xml/views/app_detail.xml");

    std::string displayTitle = package.title.empty() ? package.name : package.title;

    if (auto* frameItem = this->getAppletFrameItem())
        frameItem->title = displayTitle;

    this->titleLabel->setText(displayTitle);
    this->authorLabel->setText(package.author.empty() ? "Unknown author" : package.author);
    this->versionLabel->setText(package.version.empty() ? "Version: unknown" : "Version: " + package.version);

    std::string desc = package.long_desc.empty() ? package.short_desc : package.long_desc;
    if (desc.empty())
        desc = "No description available.";
    this->descLabel->setText(desc);

    std::string meta;
    if (!package.category.empty())
        meta += "Category: " + package.category + "\n";
    if (!package.license.empty())
        meta += "License: " + package.license + "\n";
    if (!package.updated.empty())
        meta += "Updated: " + package.updated + "\n";
    if (package.downloads > 0)
        meta += "Downloads: " + std::to_string(package.downloads) + "\n";
    if (package.download_size > 0)
        meta += "Download size: " + std::to_string(package.download_size / 1024) + " KB\n";
    if (package.extracted_size > 0)
        meta += "Installed size: " + std::to_string(package.extracted_size / 1024) + " KB\n";
    meta += "Package: " + package.name;
    this->metaLabel->setText(meta);

    // Icon: try local cache, then async download
    this->icon->clear();
    this->icon->setVisibility(brls::Visibility::VISIBLE);

    std::string repoSlug = StoreManager::repoCacheSlug(package.repo_url);
    std::string cacheDir = "sdmc:/packages/.get/packages/" + repoSlug + "/" + package.name;
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
    }
    else if (package.icon_url.empty())
    {
        this->icon->setVisibility(brls::Visibility::GONE);
    }
    else
    {
        std::string url = package.icon_url;
        std::string name = package.name;
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
                if (ok && !resolvedPath.empty() && fileExists(resolvedPath))
                    this->icon->setImageFromFile(resolvedPath);
                else
                    this->icon->setVisibility(brls::Visibility::GONE);
            }
        });
    }

    this->actionButton->registerClickAction([this](brls::View* view) {
        this->onAction();
        return true;
    });

    this->registerAction("Back", brls::ControllerButton::BUTTON_B, [this](brls::View* view) {
        this->dismiss();
        return true;
    });

    this->refreshState();
}

AppDetailView::~AppDetailView()
{
    *alive = false;
}

void AppDetailView::refreshState()
{
    // Refresh snapshot from store if available
    auto latest = StoreManager::instance().getPackage(package.name);
    if (!latest.name.empty())
        package = latest;

    if (busy)
    {
        this->statusLabel->setText("Working...");
        this->actionButton->setText("Please wait");
        this->actionButton->setState(brls::ButtonState::DISABLED);
        return;
    }

    this->actionButton->setState(brls::ButtonState::ENABLED);

    if (package.hasUpdate())
    {
        this->statusLabel->setText("Update available");
        this->actionButton->setText("Update");
    }
    else if (package.installed())
    {
        this->statusLabel->setText("Installed");
        this->actionButton->setText("Remove");
    }
    else
    {
        this->statusLabel->setText("Not installed");
        this->actionButton->setText("Install");
    }
}

void AppDetailView::onAction()
{
    if (busy)
        return;

    bool doRemove = package.installed() && !package.hasUpdate();
    busy = true;
    refreshState();

    auto name = package.name;
    auto alive = this->alive;

    auto showResult = [this, alive](bool success, const std::string& msg) {
        if (!*alive)
            return;
        busy = false;
        refreshState();
        auto* dialog = new brls::Dialog(msg);
        dialog->addButton("OK", [] {});
        dialog->open();
    };

    if (doRemove)
    {
        // Simple working dialog for remove
        auto* content = new brls::Box();
        content->setAxis(brls::Axis::COLUMN);
        content->setPadding(24, 24, 24, 24);
        content->setWidth(500);
        auto* label = new brls::Label();
        label->setText("Removing package...");
        label->setFontSize(22);
        label->setMarginBottom(12);
        content->addView(label);
        auto* spinner = new brls::ProgressSpinner();
        spinner->setWidth(48);
        spinner->setHeight(48);
        content->addView(spinner);
        content->setFocusable(true);
        content->setHideHighlight(true);
        content->registerAction("", brls::ControllerButton::BUTTON_B, [](brls::View*) { return true; }, true, false, brls::SOUND_NONE);

        auto* progressDialog = new brls::Dialog(content);
        progressDialog->setCancelable(false);
        progressDialog->setBackgroundColor(nvgRGBA(0, 0, 0, 140));
        progressDialog->open();
        brls::Application::giveFocus(content);

        StoreManager::instance().removePackage(name, [this, alive, progressDialog, showResult](bool success, const std::string& error) {
            if (!*alive)
            {
                progressDialog->close();
                return;
            }
            progressDialog->close([showResult, success, error]() {
                if (success)
                    showResult(true, "Package removed.");
                else
                    showResult(false, fmt::format("Remove failed: {}", error.empty() ? "unknown error" : error));
            });
        });
    }
    else
    {
        // Install/update with combined progress bar
        const float trackWidth = 560.0f;
        auto* content = new brls::Box();
        content->setAxis(brls::Axis::COLUMN);
        content->setPadding(24, 24, 24, 24);
        content->setWidth(620);

        auto* titleLabel = new brls::Label();
        titleLabel->setText(package.title.empty() ? package.name : package.title);
        titleLabel->setFontSize(24);
        titleLabel->setMarginBottom(8);
        content->addView(titleLabel);

        auto* phaseLabel = new brls::Label();
        phaseLabel->setText("Preparing...");
        phaseLabel->setFontSize(18);
        phaseLabel->setMarginBottom(12);
        content->addView(phaseLabel);

        auto* track = new brls::Box();
        track->setWidth(trackWidth);
        track->setHeight(18);
        track->setCornerRadius(9);
        track->setBackgroundColor(nvgRGBA(80, 80, 80, 255));

        auto* fill = new brls::Rectangle();
        fill->setWidth(0);
        fill->setHeight(18);
        fill->setCornerRadius(9);
        fill->setColor(nvgRGB(60, 140, 255));
        track->addView(fill);
        content->addView(track);

        auto* percentLabel = new brls::Label();
        percentLabel->setText("0%");
        percentLabel->setFontSize(16);
        percentLabel->setMarginTop(8);
        content->addView(percentLabel);
        content->setFocusable(true);
        content->setHideHighlight(true);
        content->registerAction("", brls::ControllerButton::BUTTON_B, [](brls::View*) { return true; }, true, false, brls::SOUND_NONE);

        auto* progressDialog = new brls::Dialog(content);
        progressDialog->setCancelable(false);
        progressDialog->setBackgroundColor(nvgRGBA(0, 0, 0, 140));
        progressDialog->open();
        brls::Application::giveFocus(content);

        auto progressAlive = std::make_shared<bool>(true);
        StoreManager::instance().installPackage(
            name,
            [this, alive, progressAlive, progressDialog, showResult](bool success, const std::string& error) {
                *progressAlive = false;
                if (!*alive)
                {
                    progressDialog->close();
                    return;
                }
                progressDialog->close([showResult, success, error]() {
                    if (success)
                        showResult(true, "Package installed successfully.");
                    else
                        showResult(false, fmt::format("Install failed: {}", error.empty() ? "unknown error" : error));
                });
            },
            [alive, progressAlive, phaseLabel, fill, percentLabel, trackWidth](float progress, const std::string& phase) {
                if (!*alive || !*progressAlive)
                    return;
                if (progress < 0) progress = 0;
                if (progress > 1) progress = 1;
                phaseLabel->setText(phase);
                fill->setWidth(trackWidth * progress);
                percentLabel->setText(fmt::format("{}%", (int)(progress * 100)));
            });
    }
}
