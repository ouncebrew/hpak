#include "tab/sources_tab.hpp"

#include <borealis/core/ime.hpp>
#include "store/store.hpp"
#include "view/source_cell.hpp"

class SourcesTab::DataSource : public brls::RecyclerDataSource
{
  public:
    std::vector<RepoSnapshot> items;
    std::function<void(const RepoSnapshot&)> onSelect;

    explicit DataSource(std::vector<RepoSnapshot> items = {}, std::function<void(const RepoSnapshot&)> onSelect = nullptr)
        : items(std::move(items)), onSelect(std::move(onSelect)) {}

    int numberOfSections(brls::RecyclerFrame* recycler) override { return 1; }
    int numberOfRows(brls::RecyclerFrame* recycler, int section) override { return (int)items.size(); }
    float heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override { return 72.0f; }

    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override
    {
        auto* cell = (SourceCell*)recycler->dequeueReusableCell("SourceCell");
        if (index.row >= 0 && index.row < (int)items.size())
            cell->setRepo(items[index.row]);
        return cell;
    }

    void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath) override
    {
        if (indexPath.row < 0 || indexPath.row >= (int)items.size())
            return;
        if (onSelect)
            onSelect(items[indexPath.row]);
    }
};

SourcesTab::SourcesTab()
{
    this->inflateFromXMLRes("xml/tabs/sources_tab.xml");

    recycler->estimatedRowHeight = 72;
    recycler->registerCell("SourceCell", []() { return SourceCell::create(); });

    dataSource = new DataSource({}, [this](const RepoSnapshot& repo) {
        this->showSourceOptions(repo);
    });
    recycler->setDataSource(dataSource, true);
    dataSource = (DataSource*)recycler->getDataSource();

    this->registerAction("Add source", brls::ControllerButton::BUTTON_Y, [this](brls::View* view) {
        this->addSource();
        return true;
    });

    loadingBadge = new LoadingBadge();
    insertBadge();

    loadData();
}

SourcesTab::~SourcesTab()
{
    *alive = false;
}

void SourcesTab::loadData()
{
    auto& store = StoreManager::instance();

    // Paint cache instantly (non-blocking).
    auto repos = store.getRepos();
    if (!repos.empty())
        refreshView();
    else
        showLoading();

    // Single-hop async load: refreshIfNeeded() only hits the network when no
    // usable data exists yet; otherwise it reports success / attaches to the
    // running load. Manual actions below still use forced refresh().
    std::weak_ptr<bool> aliveWeak = alive;
    store.refreshIfNeeded(
        [this, aliveWeak](bool success, const std::string& error) {
            auto t = aliveWeak.lock();
            if (!t || !*t)
                return;
            hideLoading();
            refreshView();
            if (!success && StoreManager::instance().getRepos().empty())
                header->setSubtitle(error.empty() ? "Failed to load sources" : error);
        },
        [this, aliveWeak](float progress, const std::string& phase) {
            auto t = aliveWeak.lock();
            if (!t || !*t)
                return;
            if (loadingBadge)
                loadingBadge->setProgress(progress, phase);
        });
}

void SourcesTab::refreshView()
{
    auto& store = StoreManager::instance();
    auto repos = store.getRepos();
    dataSource->items = repos;
    recycler->reloadData();
    if (store.isLoading())
    {
        if (repos.empty())
        {
            header->setSubtitle("Loading...");
            if (loadingBadge)
                loadingBadge->setProgress(store.getLoadProgress(), store.getLoadPhase());
        }
        else
            header->setSubtitle(std::to_string(repos.size()) + (repos.size() == 1 ? " source" : " sources"));
        return;
    }
    header->setSubtitle(std::to_string(repos.size()) + (repos.size() == 1 ? " source" : " sources"));
}

void SourcesTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    refreshView();
}

void SourcesTab::insertBadge()
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

void SourcesTab::showLoading()
{
    auto& store = StoreManager::instance();
    header->setSubtitle("Loading...");
    if (loadingBadge)
        loadingBadge->showBadge(store.getLoadPhase());
}

void SourcesTab::hideLoading()
{
    if (loadingBadge)
        loadingBadge->hideBadge();
}

void SourcesTab::addSource()
{
    auto* ime = brls::Application::getImeManager();
    if (!ime)
    {
        auto* dialog = new brls::Dialog("Text input is not available on this platform.");
        dialog->addButton("OK", [] {});
        dialog->open();
        return;
    }

    std::weak_ptr<bool> aliveWeak = alive;
    ime->openForText([this, aliveWeak](std::string text) {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;

        if (text.empty())
            return;

        auto* busyDialog = new brls::Dialog("Adding source...");
        busyDialog->setCancelable(false);
        busyDialog->open();

        StoreManager::instance().addRepo(text, [this, aliveWeak, busyDialog](bool success, const std::string& error) {
            auto token = aliveWeak.lock();
            if (!token || !*token)
            {
                busyDialog->close();
                return;
            }
            busyDialog->close([this, aliveWeak, success, error]() {
                auto token = aliveWeak.lock();
                if (!token || !*token)
                    return;
                if (!success)
                {
                    auto* dialog = new brls::Dialog("Failed to add source: " + (error.empty() ? std::string("unknown error") : error));
                    dialog->addButton("OK", [] {});
                    dialog->open();
                }
                loadData();
                // Refresh packages in background after adding
                StoreManager::instance().refresh([this, aliveWeak](bool, const std::string&) {
                    auto token = aliveWeak.lock();
                    if (!token || !*token)
                        return;
                    loadData();
                });
            });
        });
    }, "Add Source", "Enter repository base URL", 256, "https://", 0);
}

void SourcesTab::showSourceOptions(const RepoSnapshot& repo)
{
    auto* dialog = new brls::Dialog(repo.name + "\n" + repo.url);
    std::weak_ptr<bool> aliveWeak = alive;
    std::string url = repo.url;

    dialog->addButton("Refresh", [this, aliveWeak]() {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;
        StoreManager::instance().refresh([this, aliveWeak](bool success, const std::string& error) {
            auto token = aliveWeak.lock();
            if (!token || !*token)
                return;
            loadData();
            if (!success)
            {
                auto* d = new brls::Dialog("Refresh failed: " + (error.empty() ? std::string("unknown error") : error));
                d->addButton("OK", [] {});
                d->open();
            }
        });
    });

    dialog->addButton("Remove", [this, aliveWeak, url]() {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;
        StoreManager::instance().removeRepo(url, [this, aliveWeak](bool success, const std::string& error) {
            auto token = aliveWeak.lock();
            if (!token || !*token)
                return;
            if (!success)
            {
                auto* d = new brls::Dialog("Failed to remove source: " + (error.empty() ? std::string("unknown error") : error));
                d->addButton("OK", [] {});
                d->open();
            }
            loadData();
            StoreManager::instance().refresh([this, aliveWeak](bool, const std::string&) {
                auto token = aliveWeak.lock();
                if (!token || !*token)
                    return;
                loadData();
            });
        });
    });

    dialog->open();
}

brls::View* SourcesTab::create()
{
    return new SourcesTab();
}
