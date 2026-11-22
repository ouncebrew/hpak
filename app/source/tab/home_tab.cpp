#include "tab/home_tab.hpp"

#include <algorithm>
#include "store/store.hpp"
#include "view/app_cell.hpp"
#include "view/app_detail.hpp"

HomeTab::HomeTab()
{
    this->inflateFromXMLRes("xml/tabs/home_tab.xml");

    recycler->estimatedRowHeight = 78;
    recycler->registerCell("AppCell", []() { return AppCell::create(); });

    dataSource = new AppListDataSource({}, [](brls::RecyclerFrame* recycler, const PackageSnapshot& pkg) {
        recycler->present(new AppDetailView(pkg));
    });
    recycler->setDataSource(dataSource, true);
    dataSource = (AppListDataSource*)recycler->getDataSource();

    loadData();
}

HomeTab::~HomeTab()
{
    *alive = false;
}

void HomeTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    if (!dataSource)
        return;
    auto items = StoreManager::instance().getAllPackages();
    std::sort(items.begin(), items.end(), [](const PackageSnapshot& a, const PackageSnapshot& b) {
        if (a.updated != b.updated)
            return a.updated > b.updated;
        return a.downloads > b.downloads;
    });
    if (items.size() > 30)
        items.resize(30);

    if (!dataSource->sameItems(items))
    {
        dataSource->setItems(items);
        recycler->reloadData();
    }

    if (items.empty())
        header->setSubtitle("No packages yet - check Sources and refresh");
    else
        header->setSubtitle("Recently updated packages");
}

void HomeTab::loadData()
{
    header->setSubtitle("Loading featured packages...");

    auto& store = StoreManager::instance();
    store.init();

    auto refreshView = [this]() {
        auto items = StoreManager::instance().getAllPackages();
        // "Featured": prefer recently updated, then popular downloads
        std::sort(items.begin(), items.end(), [](const PackageSnapshot& a, const PackageSnapshot& b) {
            if (a.updated != b.updated)
                return a.updated > b.updated;
            return a.downloads > b.downloads;
        });
        if (items.size() > 30)
            items.resize(30);

        if (!dataSource->sameItems(items))
        {
            dataSource->setItems(items);
            recycler->reloadData();
        }

        if (items.empty())
            header->setSubtitle("No packages yet - check Sources and refresh");
        else
            header->setSubtitle("Recently updated packages");
    };

    // Show cached if available
    auto cached = store.getAllPackages();
    if (!cached.empty())
        refreshView();

    std::weak_ptr<bool> aliveWeak = alive;
    store.refresh([this, aliveWeak, refreshView](bool success, const std::string& error) {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;

        refreshView();
        if (!success)
            header->setSubtitle(error.empty() ? "Failed to load packages" : error);
    });
}

brls::View* HomeTab::create()
{
    return new HomeTab();
}
