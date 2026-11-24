#include "tab/storage_tab.hpp"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <fmt/format.h>

#include "view/storage_cell.hpp"

static uint64_t dirSize(const std::string& path)
{
    struct stat st{};
    if (lstat(path.c_str(), &st) != 0)
        return 0;
    if (S_ISLNK(st.st_mode))
        return 0; // never follow links
    if (!S_ISDIR(st.st_mode))
        return (uint64_t)st.st_size;

    uint64_t total = 0;
    DIR* dir = opendir(path.c_str());
    if (!dir)
        return 0;
    while (struct dirent* ent = readdir(dir))
    {
        std::string n = ent->d_name;
        if (n == "." || n == "..")
            continue;
        total += dirSize(path + "/" + n);
    }
    closedir(dir);
    return total;
}

static std::string formatBytesShort(uint64_t bytes)
{
    double v = (double)bytes;
    if (v >= 1024.0 * 1024.0 * 1024.0)
        return fmt::format("{:.1f} GB", v / (1024.0 * 1024.0 * 1024.0));
    if (v >= 1024.0 * 1024.0)
        return fmt::format("{:.1f} MB", v / (1024.0 * 1024.0));
    return fmt::format("{:.0f} KB", v / 1024.0);
}

void StorageDataSource::setItems(std::vector<StorageEntry> newItems, uint64_t total)
{
    items = std::move(newItems);
    totalBytes = total;
}

int StorageDataSource::numberOfSections(brls::RecyclerFrame* recycler)
{
    return 1;
}

int StorageDataSource::numberOfRows(brls::RecyclerFrame* recycler, int section)
{
    return (int)items.size();
}

brls::RecyclerCell* StorageDataSource::cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index)
{
    StorageCell* cell = (StorageCell*)recycler->dequeueReusableCell("StorageCell");
    if (index.row >= 0 && index.row < (int)items.size())
        cell->setEntry(items[index.row], totalBytes);
    return cell;
}

float StorageDataSource::heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index)
{
    return 110.0f;
}

StorageTab::StorageTab()
{
    this->inflateFromXMLRes("xml/tabs/storage_tab.xml");

    recycler->estimatedRowHeight = 110;
    recycler->registerCell("StorageCell", []() { return StorageCell::create(); });

    dataSource = new StorageDataSource();
    recycler->setDataSource(dataSource, true);
    dataSource = (StorageDataSource*)recycler->getDataSource();

    this->registerAction("Reload", brls::ControllerButton::BUTTON_X, [this](brls::View* view) {
        computeUsage();
        return true;
    });

    computeUsage();
}

StorageTab::~StorageTab()
{
    *alive = false;
}

void StorageTab::willAppear(bool resetState)
{
    brls::Box::willAppear(resetState);
}

void StorageTab::computeUsage()
{
    header->setSubtitle("Calculating...");

    std::weak_ptr<bool> aliveWeak = alive;
    brls::async([this, aliveWeak]() {
        std::vector<StorageEntry> entries = {
            {"System", "sdmc:/Nintendo", 0},
            {"Apps", "sdmc:/switch", 0},
            {"Libraries", "sdmc:/kaihen/lib", 0},
        };
        for (auto& e : entries)
            e.bytes = dirSize(e.path);

        uint64_t total = 0, freeBytes = 0;
        bool known = false;
        struct statvfs sv{};
        if (statvfs("sdmc:/", &sv) == 0 && sv.f_frsize != 0)
        {
            total = (uint64_t)sv.f_blocks * (uint64_t)sv.f_frsize;
            freeBytes = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
            known = total > 0;
        }

        brls::sync([this, aliveWeak, entries, total, freeBytes, known]() {
            auto token = aliveWeak.lock();
            if (!token || !*token || !dataSource)
                return;
            dataSource->setItems(entries, known ? total : 0);
            recycler->reloadData();
            if (known)
            {
                uint64_t used = total > freeBytes ? total - freeBytes : 0;
                header->setSubtitle(formatBytesShort(freeBytes) + " free of " + formatBytesShort(total) +
                                    " (" + formatBytesShort(used) + " used)");
            }
            else
            {
                header->setSubtitle("SD card size unknown");
            }
        });
    });
}

brls::View* StorageTab::create()
{
    return new StorageTab();
}
