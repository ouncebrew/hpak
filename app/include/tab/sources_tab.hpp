#pragma once

#include <borealis.hpp>
#include <memory>

class SourcesTab : public brls::Box
{
  public:
    SourcesTab();
    ~SourcesTab() override;
    static brls::View* create();

  private:
    void refreshInfo();
    void doRefresh();

    BRLS_BIND(brls::Label, statusLabel, "status_label");
    BRLS_BIND(brls::DetailCell, repoPath, "repo_path");
    BRLS_BIND(brls::DetailCell, repoCount, "repo_count");
    BRLS_BIND(brls::DetailCell, packageCount, "package_count");
    BRLS_BIND(brls::DetailCell, refreshAction, "refresh_action");

    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
