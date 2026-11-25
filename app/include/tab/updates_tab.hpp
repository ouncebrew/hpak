#pragma once

#include <borealis.hpp>
#include <memory>
#include "view/app_list.hpp"

class UpdatesTab : public brls::Box
{
  public:
    UpdatesTab();
    ~UpdatesTab() override;
    void willAppear(bool resetState) override;
    static brls::View* create();

  private:
    void loadData();
    void updateAll();

    BRLS_BIND(brls::RecyclerFrame, recycler, "recycler");
    BRLS_BIND(brls::Header, header, "header");

    AppListDataSource* dataSource = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
    bool busy = false;
    std::shared_ptr<std::function<void()>> updateChain;
};
