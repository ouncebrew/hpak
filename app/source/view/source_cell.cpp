#include "view/source_cell.hpp"

#include <sys/stat.h>
#include "Utils.hpp"
#include "util/icon_loader.hpp"

static bool fileExists(const std::string& path)
{
    struct stat st{};
    return stat(path.c_str(), &st) == 0 && st.st_size > 0;
}

static std::string sanitizeForPath(const std::string& s)
{
    std::string out;
    out.reserve(s.size());
    for (char c : s)
    {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
            out.push_back(c);
        else
            out.push_back('_');
    }
    return out;
}

SourceCell::SourceCell()
{
    this->inflateFromXMLRes("xml/cells/source_cell.xml");
}

SourceCell::~SourceCell()
{
    *alive = false;
}

void SourceCell::setRepo(const RepoSnapshot& r)
{
    this->repo = r;
    this->name->setText(r.name.empty() ? r.url : r.name);
    this->urlLabel->setText(r.url);

    this->icon->clear();
    // Repo icon loading disabled: loading remote repo icons can cause a crash on exit
    // in the current borealis/nanovg texture lifecycle.
    this->icon->setVisibility(brls::Visibility::GONE);
}

SourceCell* SourceCell::create()
{
    return new SourceCell();
}
