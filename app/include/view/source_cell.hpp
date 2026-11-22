#pragma once

#include <borealis.hpp>
#include <memory>
#include "store/store.hpp"

class SourceCell : public brls::RecyclerCell
{
  public:
    SourceCell();
    ~SourceCell() override;

    void setRepo(const RepoSnapshot& repo);
    RepoSnapshot getRepo() const { return repo; }

    static SourceCell* create();

  private:
    BRLS_BIND(brls::Image, icon, "icon");
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Label, urlLabel, "url");

    RepoSnapshot repo;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
