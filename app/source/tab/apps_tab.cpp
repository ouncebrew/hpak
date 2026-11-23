#include "tab/apps_tab.hpp"

#include <algorithm>
#include <cctype>
#include <map>

#include "store/store.hpp"
#include "view/category_apps_view.hpp"
#include "view/category_cell.hpp"

static std::string toLowerCat(const std::string& in)
{
    std::string out = in;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return (char)std::tolower(c); });
    return out;
}

static std::string capitalizeFirstCat(const std::string& in)
{
    if (in.empty()) return in;
    std::string out = in;
    if (std::islower((unsigned char)out[0]))
        out[0] = (char)std::toupper((unsigned char)out[0]);
    return out;
}

void CategoryListDataSource::setItems(std::vector<CategoryEntry> newItems)
{
    this->items = std::move(newItems);
}

bool CategoryListDataSource::sameItems(const std::vector<CategoryEntry>& other) const
{
    if (items.size() != other.size())
        return false;
    for (size_t i = 0; i < items.size(); i++)
    {
        if (items[i].name != other[i].name || items[i].count != other[i].count || items[i].displayName != other[i].displayName)
            return false;
    }
    return true;
}

int CategoryListDataSource::numberOfSections(brls::RecyclerFrame* recycler)
{
    return 1;
}

int CategoryListDataSource::numberOfRows(brls::RecyclerFrame* recycler, int section)
{
    return (int)items.size();
}

brls::RecyclerCell* CategoryListDataSource::cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index)
{
    CategoryCell* cell = (CategoryCell*)recycler->dequeueReusableCell("CategoryCell");
    if (index.row >= 0 && index.row < (int)items.size())
    {
        const auto& item = items[index.row];
        cell->setCategory(item.displayName, item.count);
    }
    return cell;
}

void CategoryListDataSource::didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath)
{
    if (indexPath.row < 0 || indexPath.row >= (int)items.size())
        return;
    const auto& item = items[indexPath.row];
    recycler->present(new CategoryAppsView(item.name, item.displayName));
}

float CategoryListDataSource::heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index)
{
    return 70.0f;
}

AppsTab::AppsTab()
{
    this->inflateFromXMLRes("xml/tabs/apps_tab.xml");

    recycler->estimatedRowHeight = 70;
    recycler->registerCell("CategoryCell", []() { return CategoryCell::create(); });

    dataSource = new CategoryListDataSource();
    recycler->setDataSource(dataSource, true);
    dataSource = (CategoryListDataSource*)recycler->getDataSource();

    loadData();
}

AppsTab::~AppsTab()
{
    *alive = false;
}

void AppsTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
    refreshCategories();
}

void AppsTab::refreshCategories()
{
    if (!dataSource)
        return;

    auto& store = StoreManager::instance();
    auto all = store.getAllPackages();

    std::map<std::string, CategoryEntry> byKey;
    for (auto& p : all)
    {
        std::string raw = p.category.empty() ? "Uncategorized" : p.category;
        std::string display = capitalizeFirstCat(raw);
        std::string key = toLowerCat(display);
        auto it = byKey.find(key);
        if (it == byKey.end())
        {
            CategoryEntry e;
            e.name = display; // used as filter key/display
            e.displayName = display;
            e.count = 1;
            byKey[key] = e;
        }
        else
        {
            it->second.count++;
        }
    }

    std::vector<CategoryEntry> cats;
    CategoryEntry allEntry;
    allEntry.name = ""; // empty = all
    allEntry.displayName = "All apps";
    allEntry.count = (int)all.size();
    cats.push_back(allEntry);

    for (auto& kv : byKey)
        cats.push_back(kv.second);

    if (!dataSource->sameItems(cats))
    {
        dataSource->setItems(cats);
        recycler->reloadData();
    }

    header->setSubtitle(std::to_string(cats.size() > 0 ? cats.size() - 1 : 0) + " categories");
}

void AppsTab::loadData()
{
    header->setSubtitle("Loading categories...");

    auto& store = StoreManager::instance();
    store.init();

    refreshCategories();

    std::weak_ptr<bool> aliveWeak = alive;
    store.refresh([this, aliveWeak](bool success, const std::string& error) {
        auto token = aliveWeak.lock();
        if (!token || !*token)
            return;
        refreshCategories();
        if (!success)
            header->setSubtitle(error.empty() ? "Failed to load packages" : error);
    });
}

brls::View* AppsTab::create()
{
    return new AppsTab();
}
