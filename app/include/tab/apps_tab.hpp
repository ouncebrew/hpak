#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>
#include <vector>
#include "view/loading_badge.hpp"

struct CategoryEntry
{
    std::string name;
    std::string displayName;
    int count = 0;
};

class CategoryListDataSource : public brls::RecyclerDataSource
{
  public:
    void setItems(std::vector<CategoryEntry> items);
    const std::vector<CategoryEntry>& getItems() const { return items; }
    bool sameItems(const std::vector<CategoryEntry>& other) const;

    int numberOfSections(brls::RecyclerFrame* recycler) override;
    int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
    void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath) override;
    float heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

  private:
    std::vector<CategoryEntry> items;
};

class AppsTab : public brls::Box
{
  public:
    AppsTab();
    ~AppsTab() override;
    void willAppear(bool resetState) override;
    static brls::View* create();

  private:
    void loadData();
    void refreshCategories();
    void insertBadge();
    void showLoading();
    void hideLoading();

    BRLS_BIND(brls::RecyclerFrame, recycler, "recycler");
    BRLS_BIND(brls::Header, header, "header");

    CategoryListDataSource* dataSource = nullptr;
    LoadingBadge* loadingBadge = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
