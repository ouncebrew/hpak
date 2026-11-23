#pragma once

#include <borealis.hpp>
#include <memory>

class OptionsTab : public brls::Box
{
  public:
    OptionsTab();
    static brls::View* create();

  private:
    void syncDefaultReposToggle(bool on);

    BRLS_BIND(brls::BooleanCell, optRepos, "opt_repos");
    BRLS_BIND(brls::BooleanCell, optMouse, "opt_mouse");
    BRLS_BIND(brls::BooleanCell, optLogging, "opt_logging");
};
