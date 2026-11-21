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

void AppsTab::loadData()
{
    header->setSubtitle("Loading packages...");

    auto& store = StoreManager::instance();
    store.init();

    // If we already have data, show it immediately
    auto items = store.getAllPackages();
    if (!items.empty())
    {
        dataSource->setItems(items);
        recycler->reloadData();
        header->setSubtitle(std::to_string(items.size()) + " packages available");
    }

    std::weak_ptr<bool> aliveWeak = alive;
    store.refresh([this, aliveWeak](bool success, const std::string& error) {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;

        auto items = StoreManager::instance().getAllPackages();
        dataSource->setItems(items);
        recycler->reloadData();
        if (success)
            header->setSubtitle(std::to_string(items.size()) + " packages available");
        else
            header->setSubtitle(error.empty() ? "Failed to load packages" : error);
    });
}

brls::View* AppsTab::create()
{
    return new AppsTab();
}
