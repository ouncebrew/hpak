#pragma once

#include <borealis.hpp>
#include <memory>
#include "view/app_list.hpp"

class CategoryAppsView : public brls::Box
{
  public:
    CategoryAppsView(const std::string& category, const std::string& displayName);
    ~CategoryAppsView() override;
    void willAppear(bool resetState) override;

  private:
    void applyFilter();

    std::string category;
    std::string displayName;
    std::string searchQuery;

    BRLS_BIND(brls::RecyclerFrame, recycler, "recycler");
    BRLS_BIND(brls::Header, header, "header");

    AppListDataSource* dataSource = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
