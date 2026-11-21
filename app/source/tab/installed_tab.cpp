#include "tab/installed_tab.hpp"

#include "store/store.hpp"
#include "view/app_cell.hpp"
#include "view/app_detail.hpp"

InstalledTab::InstalledTab()
{
    this->inflateFromXMLRes("xml/tabs/installed_tab.xml");

    recycler->estimatedRowHeight = 78;
    recycler->registerCell("AppCell", []() { return AppCell::create(); });

    dataSource = new AppListDataSource({}, [](brls::RecyclerFrame* recycler, const PackageSnapshot& pkg) {
        recycler->present(new AppDetailView(pkg));
    });
    recycler->setDataSource(dataSource, true);
    dataSource = (AppListDataSource*)recycler->getDataSource();

    loadData();
}

InstalledTab::~InstalledTab()
{
    *alive = false;
}

void InstalledTab::loadData()
{
    header->setSubtitle("Loading installed packages...");

    auto& store = StoreManager::instance();
    store.init();

    auto refreshView = [this]() {
        auto items = StoreManager::instance().getInstalledPackages();
        dataSource->setItems(items);
        recycler->reloadData();
        if (items.empty())
            header->setSubtitle("No installed packages found");
        else
            header->setSubtitle(std::to_string(items.size()) + " installed");
    };

    refreshView();
    std::weak_ptr<bool> aliveWeak = alive;
    store.refresh([this, aliveWeak, refreshView](bool success, const std::string& error) {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;

        refreshView();
        if (!success && StoreManager::instance().getInstalledPackages().empty())
            header->setSubtitle(error.empty() ? "Failed to load packages" : error);
    });
}

brls::View* InstalledTab::create()
{
    return new InstalledTab();
}
