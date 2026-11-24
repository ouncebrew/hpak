#pragma once

#include <borealis.hpp>
#include <memory>
#include <string>
#include <vector>
#include "view/storage_cell.hpp"

class StorageDataSource : public brls::RecyclerDataSource
{
  public:
    void setItems(std::vector<StorageEntry> items, uint64_t totalBytes);

    int numberOfSections(brls::RecyclerFrame* recycler) override;
    int numberOfRows(brls::RecyclerFrame* recycler, int section) override;
    brls::RecyclerCell* cellForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;
    float heightForRow(brls::RecyclerFrame* recycler, brls::IndexPath index) override;

  private:
    std::vector<StorageEntry> items;
    uint64_t totalBytes = 0;
};

class StorageTab : public brls::Box
{
  public:
    StorageTab();
    ~StorageTab() override;
    void willAppear(bool resetState) override;
    static brls::View* create();

  private:
    void computeUsage();

    BRLS_BIND(brls::RecyclerFrame, recycler, "recycler");
    BRLS_BIND(brls::Header, header, "header");

    StorageDataSource* dataSource = nullptr;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
