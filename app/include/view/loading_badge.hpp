#pragma once

#include <borealis.hpp>
#include <string>

class LoadingBadge : public brls::Box
{
  public:
    LoadingBadge();

    void showBadge(const std::string& phase = "Loading...");
    void setProgress(float progress, const std::string& phase);
    void hideBadge();
    bool isShowing() const { return showing; }

  private:
    brls::ProgressSpinner* spinner = nullptr;
    brls::Label* label = nullptr;
    brls::Box* track = nullptr;
    brls::Rectangle* fill = nullptr;
    bool showing = false;
};
