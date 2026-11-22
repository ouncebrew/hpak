#pragma once

#include <borealis.hpp>
#include <memory>
#include <vector>
#include "store/store.hpp"

class SourcesTab : public brls::Box
{
  public:
    SourcesTab();
    ~SourcesTab() override;
    static brls::View* create();

  private:
    void loadData();
    void addSource();
    void showSourceOptions(const RepoSnapshot& repo);

    BRLS_BIND(brls::Header, header, "header");
    BRLS_BIND(brls::RecyclerFrame, recycler, "recycler");

    class DataSource;
    DataSource* dataSource = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
