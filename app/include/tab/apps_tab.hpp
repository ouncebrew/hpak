#pragma once

#include <borealis.hpp>
#include <memory>
#include "view/app_list.hpp"

class AppsTab : public brls::Box
{
  public:
    AppsTab();
    ~AppsTab() override;
    void willAppear(bool resetState) override;
    static brls::View* create();

  private:
    void loadData();

    BRLS_BIND(brls::RecyclerFrame, recycler, "recycler");
    BRLS_BIND(brls::Header, header, "header");

    AppListDataSource* dataSource = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
