#include "view/category_apps_view.hpp"

#include <algorithm>
#include <cctype>

#include "store/store.hpp"
#include "view/app_cell.hpp"
#include "view/app_detail.hpp"

static std::string toLower(const std::string& in)
{
    std::string out = in;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

static std::string capitalizeFirst(const std::string& in)
{
    if (in.empty()) return in;
    std::string out = in;
    if (std::islower((unsigned char)out[0]))
        out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

CategoryAppsView::CategoryAppsView(const std::string& category, const std::string& displayName)
    : category(category), displayName(displayName)
{
    this->inflateFromXMLRes("xml/tabs/apps_tab.xml");

    if (auto* frameItem = this->getAppletFrameItem())
    {
        frameItem->title = "hpak";
        frameItem->setIconFromRes("icon/topIcon.png");
    }

    header->setTitle(displayName);

    recycler->estimatedRowHeight = 78;
    recycler->registerCell("AppCell", []() { return AppCell::create(); });

    dataSource = new AppListDataSource({}, [](brls::RecyclerFrame* recycler, const PackageSnapshot& pkg) {
        recycler->present(new AppDetailView(pkg));
    });
    recycler->setDataSource(dataSource, true);
    dataSource = (AppListDataSource*)recycler->getDataSource();

    this->registerAction("Search", brls::ControllerButton::BUTTON_X, [this](brls::View* view) {
        auto* ime = brls::Application::getImeManager();
        if (!ime)
            return true;

        std::weak_ptr<bool> aliveWeak = alive;
        ime->openForText([this, aliveWeak](std::string text) {
            auto token = aliveWeak.lock();
            if (!token || !*token)
                return;
            searchQuery = text;
            applyFilter();
        }, "Search", "Search packages", 64, searchQuery, 0);
        return true;
    });

    applyFilter();
}

CategoryAppsView::~CategoryAppsView()
{
    *alive = false;
}

void CategoryAppsView::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    applyFilter();
}

void CategoryAppsView::applyFilter()
{
    if (!dataSource)
        return;

    auto& store = StoreManager::instance();
    auto items = store.getAllPackages();

    if (!category.empty())
    {
        std::string catLower = toLower(category);
        std::vector<PackageSnapshot> filtered;
        for (auto& p : items)
        {
            std::string pcat = p.category.empty() ? "Uncategorized" : capitalizeFirst(p.category);
            if (toLower(pcat) == catLower)
                filtered.push_back(p);
        }
        items = std::move(filtered);
    }

    if (!searchQuery.empty())
    {
        std::string q = toLower(searchQuery);
        std::vector<PackageSnapshot> filtered;
        for (auto& p : items)
        {
            std::string hay = toLower(p.title + " " + p.name + " " + p.author + " " + p.short_desc + " " + p.category);
            if (hay.find(q) != std::string::npos)
                filtered.push_back(p);
        }
        items = std::move(filtered);
    }

    if (!dataSource->sameItems(items))
    {
        dataSource->setItems(items);
        recycler->reloadData();
    }

    if (searchQuery.empty())
        header->setSubtitle(std::to_string(items.size()) + (items.size() == 1 ? " app" : " apps"));
    else
        header->setSubtitle(std::to_string(items.size()) + " results for \"" + searchQuery + "\"");
}
