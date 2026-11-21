#pragma once

#include <borealis.hpp>
#include <functional>
#include <vector>
#include "store/store.hpp"

class AppListDataSource : public brls::RecyclerDataSource
{
  public:
    using SelectCallback = std::function<void(brls::RecyclerFrame*, const PackageSnapshot&)>;

    explicit AppListDataSource(std::vector<PackageSnapshot> items = {}, SelectCallback onSelect = nullptr);

    void setItems(std::vector<PackageSnapshot> items);
    const std::vector<PackageSnapshot>& getItems() const { return items; }

    int numberOfSections(brls::RecyclerFrame* recycler) override;
    int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
    void didSelectRowAt(brls::RecyclerFrame* recycler, brls::IndexPath indexPath) override;
    float heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

  private:
    std::vector<PackageSnapshot> items;
    SelectCallback onSelect;
};
