#pragma once

#include <borealis.hpp>

class CategoryCell : public brls::RecyclerCell
{
  public:
    CategoryCell();
    static CategoryCell* create();
    void setCategory(const std::string& name, int count);

  private:
    BRLS_BIND(brls::Label, name, "name");
    BRLS_BIND(brls::Label, subtitle, "subtitle");
};
