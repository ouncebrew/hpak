#pragma once

#include <borealis.hpp>
#include <memory>
#include "view/app_list.hpp"
#include "view/loading_badge.hpp"

class InstalledTab : public brls::Box
{
  public:
    InstalledTab();
    ~InstalledTab() override;
    void willAppear(bool resetState) override;
    static brls::View* create();

  private:
    void loadData();
    void refreshView();
    void insertBadge();
    void showLoading();
    void hideLoading();

    BRLS_BIND(brls::RecyclerFrame, recycler, "recycler");
    BRLS_BIND(brls::Header, header, "header");

    AppListDataSource* dataSource = nullptr;
    LoadingBadge* loadingBadge = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
