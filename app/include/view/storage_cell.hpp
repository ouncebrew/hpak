#pragma once

#include <borealis.hpp>
#include <cstdint>
#include <string>

struct StorageEntry
{
    std::string name;
    std::string path;
    uint64_t bytes = 0;
};

class StorageCell : public brls::RecyclerCell
{
  public:
    StorageCell();
    static StorageCell* create();
    void setEntry(const StorageEntry& entry, uint64_t totalBytes);

  private:
    BRLS_BIND(brls::Label, title, "title");
    BRLS_BIND(brls::Label, size, "size");
    BRLS_BIND(brls::Label, subtitle, "subtitle");
    BRLS_BIND(brls::Box, bar, "bar");

    void rebuildBar(float fraction);
};
