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

    loadingBadge = new LoadingBadge();
    insertBadge();

    loadData();
}

HomeTab::~HomeTab()
{
    *alive = false;
}

void HomeTab::insertBadge()
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

void HomeTab::showLoading()
{
    auto& store = StoreManager::instance();
    if (loadingBadge)
        loadingBadge->showBadge(store.getLoadPhase());
    header->setSubtitle("Loading...");
}

void HomeTab::hideLoading()
{
    if (loadingBadge)
        loadingBadge->hideBadge();
}

void HomeTab::refreshView()
{
    auto items = StoreManager::instance().getAllPackages();
    // "Featured": prefer recently updated, then popular downloads
    std::sort(items.begin(), items.end(), [](const PackageSnapshot& a, const PackageSnapshot& b) {
        if (a.updated_timestamp != b.updated_timestamp)
            return a.updated_timestamp > b.updated_timestamp;
        return a.downloads > b.downloads;
    });
    if (items.size() > 5)
        items.resize(5);

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

void HomeTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    if (!dataSource)
        return;
    auto& store = StoreManager::instance();
    if (store.isLoading())
    {
        // First repo load still running on a background thread: keep the UI
        // responsive and show the determinate progress badge instead of stale
        // or empty content.
        auto items = store.getAllPackages();
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
    auto items = store.getAllPackages();
    std::sort(items.begin(), items.end(), [](const PackageSnapshot& a, const PackageSnapshot& b) {
        if (a.updated_timestamp != b.updated_timestamp)
            return a.updated_timestamp > b.updated_timestamp;
        return a.downloads > b.downloads;
    });
    if (items.size() > 5)
        items.resize(5);

    if (items.empty() && !refreshSucceeded && !store.isLoading())
    {
        // First load produced nothing (e.g. refresh ran before network was up)
        // and no refresh is currently in flight — retry the load.
        loadData();
        return;
    }

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
    auto& store = StoreManager::instance();

    // Paint whatever is cached instantly (pure cache read, never blocks), so
    // the first paint never freezes even before the repo load finishes.
    auto cached = store.getAllPackages();
    if (!cached.empty())
        refreshView();
    else
        showLoading();

    // Fully async first load with a single hop onto the background thread:
    // refreshIfNeeded() starts a network load only when no usable data exists
    // yet; otherwise it just reports success or attaches to the running load.
    // Progress lands on the badge via the progress callback.
    std::weak_ptr<bool> aliveWeak = alive;
    store.refreshIfNeeded(
        [this, aliveWeak](bool success, const std::string& error) {
            auto t = aliveWeak.lock();
            if (!t || !*t)
                return;
            hideLoading();
            if (success)
                refreshSucceeded = true;
            refreshView();
            if (!success)
                header->setSubtitle(error.empty() ? "Failed to load packages" : error);
        },
        [this, aliveWeak](float progress, const std::string& phase) {
            auto t = aliveWeak.lock();
            if (!t || !*t)
                return;
            // Already on the UI thread (refresh dispatches via brls::sync).
            if (loadingBadge)
                loadingBadge->setProgress(progress, phase);
        });
}

brls::View* HomeTab::create()
{
    return new HomeTab();
}
