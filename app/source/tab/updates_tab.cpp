#include "tab/updates_tab.hpp"

#include <fmt/format.h>

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

    this->registerAction("Update All", brls::ControllerButton::BUTTON_X, [this](brls::View* view) {
        this->updateAll();
        return true;
    });

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

void UpdatesTab::updateAll()
{
    if (busy)
        return;
    auto items = StoreManager::instance().getUpdatablePackages();
    if (items.empty() || !dataSource)
        return;
    busy = true;

    auto alive = this->alive;
    auto remaining = std::make_shared<std::vector<PackageSnapshot>>(std::move(items));
    auto failed = std::make_shared<std::vector<std::string>>();
    auto skipped = std::make_shared<std::vector<std::string>>();
    int total = (int)remaining->size();

    // Shared progress dialog for the whole batch.
    const float trackWidth = 560.0f;
    auto* content = new brls::Box();
    content->setAxis(brls::Axis::COLUMN);
    content->setPadding(24, 24, 24, 24);
    content->setWidth(620);

    auto* titleLabel = new brls::Label();
    titleLabel->setText("Updating...");
    titleLabel->setFontSize(24);
    titleLabel->setMarginBottom(8);
    content->addView(titleLabel);

    auto* phaseLabel = new brls::Label();
    phaseLabel->setText("Preparing...");
    phaseLabel->setFontSize(18);
    phaseLabel->setMarginBottom(12);
    content->addView(phaseLabel);

    auto* track = new brls::Box();
    track->setWidth(trackWidth);
    track->setHeight(18);
    track->setCornerRadius(9);
    track->setBackgroundColor(nvgRGBA(80, 80, 80, 255));

    auto* fill = new brls::Rectangle();
    fill->setWidth(0);
    fill->setHeight(18);
    fill->setCornerRadius(9);
    fill->setColor(nvgRGB(60, 140, 255));
    track->addView(fill);
    content->addView(track);

    auto* percentLabel = new brls::Label();
    percentLabel->setText("0%");
    percentLabel->setFontSize(16);
    percentLabel->setMarginTop(8);
    content->addView(percentLabel);
    content->setFocusable(true);
    content->setHideHighlight(true);
    content->registerAction("", brls::ControllerButton::BUTTON_B, [](brls::View*) { return true; }, true, false, brls::SOUND_NONE);

    auto* progressDialog = new brls::Dialog(content);
    progressDialog->setCancelable(false);
    progressDialog->setBackgroundColor(nvgRGBA(0, 0, 0, 140));
    progressDialog->open();
    brls::Application::giveFocus(content);

    auto finish = [this, alive, progressDialog, failed, skipped, total]() {
        busy = false;
        updateChain.reset();
        if (!*alive)
        {
            progressDialog->close();
            return;
        }
        auto refreshItems = StoreManager::instance().getUpdatablePackages();
        if (dataSource && !dataSource->sameItems(refreshItems))
        {
            dataSource->setItems(refreshItems);
            recycler->reloadData();
        }
        if (refreshItems.empty())
            header->setSubtitle("Everything is up to date");
        else
            header->setSubtitle(std::to_string(refreshItems.size()) + " updates available");

        std::string msg;
        if (failed->empty() && skipped->empty())
            msg = "All updates installed.";
        else
        {
            int done = total - (int)failed->size() - (int)skipped->size();
            msg = fmt::format("Updated {} of {}.", done, total);
            if (!failed->empty())
            {
                msg += "\nFailed: ";
                for (size_t i = 0; i < failed->size(); i++)
                    msg += (i ? ", " : "") + (*failed)[i];
            }
            if (!skipped->empty())
            {
                msg += "\nSkipped (breaks installed dependents): ";
                for (size_t i = 0; i < skipped->size(); i++)
                    msg += (i ? ", " : "") + (*skipped)[i];
            }
        }
        progressDialog->close([this, alive, msg]() {
            if (!*alive)
                return;
            auto* dialog = new brls::Dialog(msg);
            dialog->addButton("OK", [] {});
            dialog->open();
        });
    };

    auto step = std::make_shared<std::function<void()>>();
    updateChain = step;
    *step = [this, alive, remaining, failed, skipped, total, titleLabel, phaseLabel, fill, percentLabel, trackWidth, progressDialog, finish]() {
        if (remaining->empty())
        {
            finish();
            return;
        }
        PackageSnapshot pkg = remaining->front();
        remaining->erase(remaining->begin());
        int done = total - (int)remaining->size();
        float p0 = (float)(done - 1) / total;
        float p1 = (float)done / total;

        auto next = [this]() {
            if (updateChain && *updateChain)
                (*updateChain)();
        };

        // Skip updates that would violate an installed dependent's constraint.
        auto viol = StoreManager::instance().getDependencyViolations(pkg.name, pkg.version);
        if (!viol.empty())
        {
            std::string who;
            for (size_t i = 0; i < viol.size(); i++)
                who += (i ? ", " : "") + viol[i].first;
            skipped->push_back(pkg.name + " (would break " + who + ")");
            next();
            return;
        }

        std::string displayTitle = pkg.title.empty() ? pkg.name : pkg.title;
        brls::sync([titleLabel, displayTitle, done, total]() {
            titleLabel->setText(fmt::format("Updating {} ({}/{})", displayTitle, done, total));
        });
        StoreManager::instance().installPackage(pkg.name,
            [this, alive, next, failed, pkg](bool ok, const std::string& error) {
                if (!*alive)
                    return;
                if (!ok)
                    failed->push_back(pkg.name);
                next();
            },
            [alive, p0, p1, phaseLabel, fill, percentLabel, trackWidth](float prog, const std::string& phase) {
                if (!*alive)
                    return;
                if (prog < 0) prog = 0;
                if (prog > 1) prog = 1;
                float p = p0 + prog * (p1 - p0);
                phaseLabel->setText(phase);
                fill->setWidth(trackWidth * p);
                percentLabel->setText(fmt::format("{}%", (int)(p * 100)));
            });
    };
    (*step)();
}

brls::View* UpdatesTab::create()
{
    return new UpdatesTab();
}
