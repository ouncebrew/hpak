#pragma once

#include <borealis.hpp>
#include <memory>
#include <vector>
#include "store/store.hpp"
#include "view/loading_badge.hpp"

class SourcesTab : public brls::Box
{
  public:
    SourcesTab();
    ~SourcesTab() override;
    void willAppear(bool resetState) override;
    static brls::View* create();

  private:
    void loadData();
    void refreshView();
    void insertBadge();
    void showLoading();
    void hideLoading();
    void addSource();
    void showSourceOptions(const RepoSnapshot& repo);

    BRLS_BIND(brls::Header, header, "header");
    BRLS_BIND(brls::RecyclerFrame, recycler, "recycler");

    class DataSource;
    DataSource* dataSource = nullptr;
    LoadingBadge* loadingBadge = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
