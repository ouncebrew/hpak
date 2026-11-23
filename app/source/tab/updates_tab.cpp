#include "tab/updates_tab.hpp"

#include "store/store.hpp"
#include "view/app_cell.hpp"
#include "view/app_detail.hpp"

UpdatesTab::UpdatesTab()
{
    this->inflateFromXMLRes("xml/tabs/updates_tab.xml");

    recycler->estimatedRowHeight = 78;
    recycler->registerCell("AppCell", []() { return AppCell::create(); });

    dataSource = new AppListDataSource({}, [](brls::RecyclerFrame* recycler, const PackageSnapshot& pkg) {
        recycler->present(new AppDetailView(pkg));
    });
    recycler->setDataSource(dataSource, true);
    dataSource = (AppListDataSource*)recycler->getDataSource();

    loadData();
}

UpdatesTab::~UpdatesTab()
{
    *alive = false;
}

void UpdatesTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    if (!dataSource)
        return;
    auto items = StoreManager::instance().getUpdatablePackages();
    if (!dataSource->sameItems(items))
    {
        dataSource->setItems(items);
        recycler->reloadData();
    }
    if (items.empty())
        header->setSubtitle("Everything is up to date");
    else
        header->setSubtitle(std::to_string(items.size()) + " updates available");
}

void UpdatesTab::loadData()
{
    header->setSubtitle("Checking for updates...");

    auto& store = StoreManager::instance();
    store.init();

    auto refreshView = [this]() {
        auto items = StoreManager::instance().getUpdatablePackages();
        if (!dataSource->sameItems(items))
        {
            dataSource->setItems(items);
            recycler->reloadData();
        }
        if (items.empty())
            header->setSubtitle("Everything is up to date");
        else
            header->setSubtitle(std::to_string(items.size()) + " updates available");
    };

    refreshView();
    std::weak_ptr<bool> aliveWeak = alive;
    store.refresh([this, aliveWeak, refreshView](bool success, const std::string& error) {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;

        refreshView();
        if (!success && StoreManager::instance().getUpdatablePackages().empty())
            header->setSubtitle(error.empty() ? "Failed to load packages" : error);
    });
}

brls::View* UpdatesTab::create()
{
    return new UpdatesTab();
}
