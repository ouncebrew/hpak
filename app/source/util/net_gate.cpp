#include "util/net_gate.hpp"

#include <chrono>
#include <thread>

#include <borealis.hpp>

#if defined(__SWITCH__)
#include <switch.h>
#endif

namespace util
{

bool isOnline()
{
#if defined(__SWITCH__)
    NifmInternetConnectionType type = (NifmInternetConnectionType)0;
    u32 strength = 0;
    NifmInternetConnectionStatus status = (NifmInternetConnectionStatus)0;
    if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &status)))
        return false;
    return status == NifmInternetConnectionStatus_Connected;
#else
    return true;
#endif
}

bool waitForConnection(const std::function<bool()>& shouldAbort)
{
    if (isOnline())
        return true;

    brls::Dialog* dlg = nullptr;
    brls::sync([&dlg]() {
        auto* box = new brls::Box();
        box->setAxis(brls::Axis::COLUMN);
        box->setPadding(24, 24, 24, 24);
        box->setWidth(560);
        auto* title = new brls::Label();
        title->setText("No internet connection");
        title->setFontSize(24);
        title->setMarginBottom(8);
        box->addView(title);
        auto* detail = new brls::Label();
        detail->setText("Connect to Wi-Fi or Ethernet to continue.");
        detail->setFontSize(18);
        box->addView(detail);
        dlg = new brls::Dialog(box);
        dlg->setCancelable(false);
        dlg->open();
    });

    while (!isOnline())
    {
        if (shouldAbort && shouldAbort())
            break;
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    bool connected = isOnline();
    brls::sync([dlg]() {
        if (dlg)
            dlg->close();
    });
    return connected;
}

} // namespace util
