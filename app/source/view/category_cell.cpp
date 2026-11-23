#include "view/category_cell.hpp"

CategoryCell::CategoryCell()
{
    this->inflateFromXMLRes("xml/cells/category_cell.xml");
}

CategoryCell* CategoryCell::create()
{
    return new CategoryCell();
}

void CategoryCell::setCategory(const std::string& name, int count)
{
    this->name->setText(name);
    this->subtitle->setText(std::to_string(count) + (count == 1 ? " app" : " apps"));
}
