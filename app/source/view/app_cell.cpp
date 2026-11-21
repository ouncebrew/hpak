#include "view/app_cell.hpp"

AppCell::AppCell()
{
    this->inflateFromXMLRes("xml/cells/app_cell.xml");
}

void AppCell::setPackage(const PackageSnapshot& pkg)
{
    this->package = pkg;

    std::string displayTitle = pkg.title.empty() ? pkg.name : pkg.title;
    this->title->setText(displayTitle);
    this->version->setText(pkg.version.empty() ? "" : "v" + pkg.version);

    std::string sub = pkg.author.empty() ? "" : pkg.author;
    if (!pkg.short_desc.empty())
    {
        if (!sub.empty())
            sub += " • ";
        sub += pkg.short_desc;
    }
    if (sub.empty() && !pkg.category.empty())
        sub = pkg.category;
    this->subtitle->setText(sub);

    if (pkg.hasUpdate())
        this->status->setText("Update");
    else if (pkg.installed())
        this->status->setText("Installed");
    else
        this->status->setText("");
}

AppCell* AppCell::create()
{
    return new AppCell();
}
