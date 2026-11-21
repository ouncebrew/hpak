#include "view/app_detail.hpp"

#include <fmt/format.h>

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
    if (doRemove)
    {
        StoreManager::instance().removePackage(name, [this, alive](bool success, const std::string& error) {
            if (!*alive)
                return;
            busy = false;
            refreshState();
            if (success)
            {
                auto* dialog = new brls::Dialog("Package removed.");
                dialog->addButton("OK", [] {});
                dialog->open();
            }
            else
            {
                auto* dialog = new brls::Dialog(fmt::format("Remove failed: {}", error.empty() ? "unknown error" : error));
                dialog->addButton("OK", [] {});
                dialog->open();
            }
        });
    }
    else
    {
        StoreManager::instance().installPackage(name, [this, alive](bool success, const std::string& error) {
            if (!*alive)
                return;
            busy = false;
            refreshState();
            if (success)
            {
                auto* dialog = new brls::Dialog("Package installed successfully.");
                dialog->addButton("OK", [] {});
                dialog->open();
            }
            else
            {
                auto* dialog = new brls::Dialog(fmt::format("Install failed: {}", error.empty() ? "unknown error" : error));
                dialog->addButton("OK", [] {});
                dialog->open();
            }
        });
    }
}
