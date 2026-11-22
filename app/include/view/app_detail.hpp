#pragma once

#include <borealis.hpp>
#include <memory>
#include "store/store.hpp"

class AppDetailView : public brls::Box
{
  public:
    explicit AppDetailView(const PackageSnapshot& pkg);
    ~AppDetailView() override;

  private:
    void refreshState();
    void onAction();

    BRLS_BIND(brls::ScrollingFrame, scroll, "detail_scroll");
    BRLS_BIND(brls::Image, icon, "detail_icon");
    BRLS_BIND(brls::Label, titleLabel, "detail_title");
    BRLS_BIND(brls::Label, authorLabel, "detail_author");
    BRLS_BIND(brls::Label, versionLabel, "detail_version");
    BRLS_BIND(brls::Label, statusLabel, "detail_status");
    BRLS_BIND(brls::Label, descLabel, "detail_desc");
    BRLS_BIND(brls::Label, metaLabel, "detail_meta");
    BRLS_BIND(brls::Button, actionButton, "detail_action");

    PackageSnapshot package;
    bool busy = false;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};
