#pragma once

#include <borealis.hpp>
#include "store/store.hpp"

class AppCell : public brls::RecyclerCell
{
  public:
    AppCell();

    void setPackage(const PackageSnapshot& pkg);
    PackageSnapshot getPackage() const { return package; }

    static AppCell* create();

  private:
    BRLS_BIND(brls::Label, title, "title");
    BRLS_BIND(brls::Label, version, "version");
    BRLS_BIND(brls::Label, subtitle, "subtitle");
    BRLS_BIND(brls::Label, status, "status");

    PackageSnapshot package;
};
