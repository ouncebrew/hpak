#include "view/storage_cell.hpp"

#include <fmt/format.h>

static std::string formatBytes(uint64_t bytes)
{
    double v = (double)bytes;
    if (v >= 1024.0 * 1024.0 * 1024.0)
        return fmt::format("{:.1f} GB", v / (1024.0 * 1024.0 * 1024.0));
    if (v >= 1024.0 * 1024.0)
        return fmt::format("{:.1f} MB", v / (1024.0 * 1024.0));
    if (v >= 1024.0)
        return fmt::format("{:.0f} KB", v / 1024.0);
    return fmt::format("{} B", bytes);
}

StorageCell::StorageCell()
{
    this->inflateFromXMLRes("xml/cells/storage_cell.xml");
}

StorageCell* StorageCell::create()
{
    return new StorageCell();
}

void StorageCell::rebuildBar(float fraction)
{
    if (fraction < 0.0f) fraction = 0.0f;
    if (fraction > 1.0f) fraction = 1.0f;

    const float trackWidth = 560.0f;
    bar->clearViews();

    auto* track = new brls::Box();
    track->setWidth(trackWidth);
    track->setHeight(14);
    track->setCornerRadius(7);
    track->setBackgroundColor(nvgRGBA(128, 128, 128, 90));

    auto* fill = new brls::Rectangle();
    fill->setWidth(trackWidth * fraction);
    fill->setHeight(14);
    fill->setCornerRadius(7);
    fill->setColor(brls::Application::getTheme()["brls/accent"]);
    track->addView(fill);

    bar->addView(track);
}

void StorageCell::setEntry(const StorageEntry& entry, uint64_t totalBytes)
{
    title->setText(entry.name);

    float pct = 0.0f;
    std::string sub = entry.path;
    if (totalBytes > 0)
    {
        pct = (float)((double)entry.bytes / (double)totalBytes);
        int pctInt = (int)(pct * 100.0f + 0.5f);
        sub += " (" + std::to_string(pctInt) + "%)";
    }

    size->setText(formatBytes(entry.bytes));
    subtitle->setText(sub);
    rebuildBar(pct);
}
