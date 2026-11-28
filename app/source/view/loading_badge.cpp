#include "view/loading_badge.hpp"

#include <fmt/format.h>

LoadingBadge::LoadingBadge()
    : brls::Box(brls::Axis::COLUMN)
{
    this->setVisibility(brls::Visibility::GONE);
    this->setFocusable(false);
    this->setHideHighlight(true);
    this->setMargins(4, 24, 8, 24);

    auto* row = new brls::Box(brls::Axis::ROW);
    row->setFocusable(false);
    row->setHideHighlight(true);
    row->setAlignItems(brls::AlignItems::CENTER);
    row->setWidth(brls::View::AUTO);
    row->setHeight(brls::View::AUTO);
    row->setMarginBottom(6);

    spinner = new brls::ProgressSpinner();
    spinner->setFocusable(false);
    spinner->setHideHighlight(true);
    spinner->setWidth(28);
    spinner->setHeight(28);
    row->addView(spinner);

    label = new brls::Label();
    label->setFocusable(false);
    label->setHideHighlight(true);
    label->setFontSize(18);
    label->setMarginLeft(12);
    label->setText("Loading...");
    row->addView(label);

    this->addView(row);

    track = new brls::Box(brls::Axis::ROW);
    track->setFocusable(false);
    track->setHideHighlight(true);
    track->setWidth(brls::View::AUTO);
    track->setGrow(1.0f);
    track->setHeight(12);
    track->setCornerRadius(6);
    track->setBackgroundColor(nvgRGBA(80, 80, 80, 255));
    track->setClipsToBounds(true);

    fill = new brls::Rectangle(nvgRGB(60, 140, 255));
    fill->setFocusable(false);
    fill->setHideHighlight(true);
    fill->setHeight(12);
    fill->setCornerRadius(6);
    fill->setWidthPercentage(0.0f);
    track->addView(fill);

    this->addView(track);
}

void LoadingBadge::showBadge(const std::string& phase)
{
    showing = true;
    setProgress(0.0f, phase);
    this->setVisibility(brls::Visibility::VISIBLE);
}

void LoadingBadge::setProgress(float progress, const std::string& phase)
{
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;
    if (!showing)
    {
        showing = true;
        this->setVisibility(brls::Visibility::VISIBLE);
    }
    if (label)
        label->setText(fmt::format("{} {}%", phase.empty() ? "Loading..." : phase, (int)(progress * 100)));
    if (fill)
        fill->setWidthPercentage(progress * 100.0f);
}

void LoadingBadge::hideBadge()
{
    showing = false;
    this->setVisibility(brls::Visibility::GONE);
}
