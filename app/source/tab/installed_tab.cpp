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

    loadingBadge = new LoadingBadge();
    insertBadge();

    loadData();
}

InstalledTab::~InstalledTab()
{
    *alive = false;
}

void InstalledTab::insertBadge()
{
    if (!loadingBadge)
        return;
    auto& kids = this->getChildren();
    size_t pos = kids.size();
    for (size_t i = 0; i < kids.size(); i++)
    {
        if (kids[i] == header)
        {
            pos = i + 1;
            break;
        }
    }
    this->addView(loadingBadge, pos);
}

void InstalledTab::showLoading()
{
    auto& store = StoreManager::instance();
    header->setSubtitle("Loading...");
    if (loadingBadge)
        loadingBadge->showBadge(store.getLoadPhase());
}

void InstalledTab::hideLoading()
{
    if (loadingBadge)
        loadingBadge->hideBadge();
}

void InstalledTab::refreshView()
{
    auto items = StoreManager::instance().getInstalledPackages();
    if (!dataSource->sameItems(items))
    {
        dataSource->setItems(items);
        recycler->reloadData();
    }
    if (items.empty())
        header->setSubtitle("No installed packages found");
    else
        header->setSubtitle(std::to_string(items.size()) + " installed");
}

void InstalledTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    if (!dataSource)
        return;
    auto& store = StoreManager::instance();
    if (store.isLoading())
    {
        auto items = store.getInstalledPackages();
        if (!items.empty())
            refreshView();
        else
        {
            if (loadingBadge)
                loadingBadge->setProgress(store.getLoadProgress(), store.getLoadPhase());
            header->setSubtitle("Loading...");
        }
        return;
    }
    refreshView();
}

void InstalledTab::loadData()
{
    auto& store = StoreManager::instance();

    auto cached = store.getInstalledPackages();
    if (!cached.empty())
        refreshView();
    else
        showLoading();

    std::weak_ptr<bool> aliveWeak = alive;
    store.refreshIfNeeded(
        [this, aliveWeak](bool success, const std::string& error) {
            auto t = aliveWeak.lock();
            if (!t || !*t)
                return;
            hideLoading();
            refreshView();
            if (!success && StoreManager::instance().getInstalledPackages().empty())
                header->setSubtitle(error.empty() ? "Failed to load packages" : error);
        },
        [this, aliveWeak](float progress, const std::string& phase) {
            auto t = aliveWeak.lock();
            if (!t || !*t)
                return;
            if (loadingBadge)
                loadingBadge->setProgress(progress, phase);
        });
}

brls::View* InstalledTab::create()
{
    return new InstalledTab();
}
