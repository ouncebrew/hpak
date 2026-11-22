#include "view/app_list.hpp"
#include "view/app_cell.hpp"

AppListDataSource::AppListDataSource(std::vector<PackageSnapshot> items, SelectCallback onSelect)
    : items(std::move(items)), onSelect(std::move(onSelect))
{
}

void AppListDataSource::setItems(std::vector<PackageSnapshot> newItems)
{
    this->items = std::move(newItems);
}

bool AppListDataSource::sameItems(const std::vector<PackageSnapshot>& other) const
{
    if (items.size() != other.size())
        return false;
    for (size_t i = 0; i < items.size(); i++)
    {
        const auto& a = items[i];
        const auto& b = other[i];
        if (a.name != b.name || a.version != b.version || a.status != b.status || a.title != b.title)
            return false;
    }
    return true;
}

int AppListDataSource::numberOfSections(brls::RecyclerFrame* recycler)
{
    return 1;
}

int AppListDataSource::numberOfRows(brls::RecyclerFrame* recycler, int section)
{
    return (int)items.size();
}

brls::RecyclerCell* AppListDataSource::cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index)
{
    AppCell* cell = (AppCell*)recycler->dequeueReusableCell("AppCell");
    if (index.row >= 0 && index.row < (int)items.size())
        cell->setPackage(items[index.row]);
    return cell;
}

void AppListDataSource::didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath)
{
    if (indexPath.row < 0 || indexPath.row >= (int)items.size())
        return;
    if (onSelect)
        onSelect(recycler, items[indexPath.row]);
}

float AppListDataSource::heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index)
{
    return 78.0f;
}
