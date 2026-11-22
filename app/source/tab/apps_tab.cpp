#include "tab/apps_tab.hpp"

#include "store/store.hpp"
#include "view/app_cell.hpp"
#include "view/app_detail.hpp"

AppsTab::AppsTab()
{
    this->inflateFromXMLRes("xml/tabs/apps_tab.xml");

    recycler->estimatedRowHeight = 78;
    recycler->registerCell("AppCell", []() { return AppCell::create(); });

    dataSource = new AppListDataSource({}, [](brls::RecyclerFrame* recycler, const PackageSnapshot& pkg) {
        recycler->present(new AppDetailView(pkg));
    });
    recycler->setDataSource(dataSource, true);
    dataSource = (AppListDataSource*)recycler->getDataSource();

    loadData();
}

AppsTab::~AppsTab()
{
    *alive = false;
}

void AppsTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    if (!dataSource)
        return;
    auto items = StoreManager::instance().getAllPackages();
    if (!dataSource->sameItems(items))
    {
        dataSource->setItems(items);
        recycler->reloadData();
    }
    header->setSubtitle(std::to_string(items.size()) + " packages available");
}

void AppsTab::loadData()
{
    header->setSubtitle("Loading packages...");

    auto& store = StoreManager::instance();
    store.init();

    auto refreshView = [this](bool afterRefresh, bool success, const std::string& error) {
        auto items = StoreManager::instance().getAllPackages();
        if (!dataSource->sameItems(items))
        {
            dataSource->setItems(items);
            recycler->reloadData();
        }
        if (afterRefresh && !success)
            header->setSubtitle(error.empty() ? "Failed to load packages" : error);
        else
            header->setSubtitle(std::to_string(items.size()) + " packages available");
    };

    // If we already have data, show it immediately
    refreshView(false, true, "");

    std::weak_ptr<bool> aliveWeak = alive;
    store.refresh([this, aliveWeak, refreshView](bool success, const std::string& error) {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;

        refreshView(true, success, error);
    });
}

brls::View* AppsTab::create()
{
    return new AppsTab();
}
