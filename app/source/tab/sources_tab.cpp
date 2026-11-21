#include "tab/sources_tab.hpp"

#include "store/store.hpp"

SourcesTab::SourcesTab()
{
    this->inflateFromXMLRes("xml/tabs/sources_tab.xml");

    auto& store = StoreManager::instance();

    repoPath->setDetailText(store.getConfigPath());
    repoCount->setDetailText("From repos.json");
    packageCount->setDetailText("Not loaded");

    refreshAction->setDetailText("Press A");
    refreshAction->registerClickAction([this](brls::View* view) {
        this->doRefresh();
        return true;
    });

    refreshInfo();
}

SourcesTab::~SourcesTab()
{
    *alive = false;
}

void SourcesTab::refreshInfo()
{
    auto& store = StoreManager::instance();
    auto all = store.getAllPackages();
    auto installed = store.getInstalledPackages();

    repoPath->setDetailText(store.getConfigPath());
    packageCount->setDetailText(std::to_string(all.size()) + " total, " + std::to_string(installed.size()) + " installed");
    repoCount->setDetailText("From repos.json");

    if (store.isLoading())
    {
        statusLabel->setText("Refreshing repositories...");
    }
    else if (!store.getLastError().empty() && all.empty())
    {
        statusLabel->setText("Last error: " + store.getLastError());
    }
    else if (all.empty())
    {
        statusLabel->setText("No packages loaded yet. Press A on Refresh repositories to fetch package data.");
    }
    else
    {
        statusLabel->setText("Repositories loaded successfully.");
    }
}

void SourcesTab::doRefresh()
{
    auto& store = StoreManager::instance();
    store.init();
    statusLabel->setText("Refreshing repositories...");

    std::weak_ptr<bool> aliveWeak = alive;
    store.refresh([this, aliveWeak](bool success, const std::string& error) {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;

        if (success)
        {
            statusLabel->setText("Repositories refreshed successfully.");
        }
        else
        {
            statusLabel->setText("Refresh failed: " + (error.empty() ? std::string("unknown error") : error));
            auto* dialog = new brls::Dialog("Failed to refresh repositories: " + (error.empty() ? std::string("unknown error") : error));
            dialog->addButton("OK", [] {});
            dialog->open();
        }
        refreshInfo();
    });
}

brls::View* SourcesTab::create()
{
    return new SourcesTab();
}
