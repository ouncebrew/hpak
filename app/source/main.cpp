#include <borealis.hpp>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

#include "activity/main_activity.hpp"
#include "store/store.hpp"
#include "tab/apps_tab.hpp"
#include "tab/home_tab.hpp"
#include "tab/installed_tab.hpp"
#include "tab/sources_tab.hpp"
#include "tab/updates_tab.hpp"
#include "util/icon_loader.hpp"

#if defined(__PSV__) && defined(BOREALIS_USE_OPENGL)
extern "C" unsigned int sceLibcHeapSize = 2 * 1024 * 1024;
#endif

int main(int argc, char* argv[])
{
    // Log to SD card for debugging (nxlink may be unavailable)
    if (std::freopen("sdmc:/hpak.txt", "w", stdout))
    {
        setvbuf(stdout, NULL, _IONBF, 0);
        dup2(fileno(stdout), fileno(stderr));
        setvbuf(stderr, NULL, _IONBF, 0);
        std::printf("=== hpak log start ===\n");
    }

    for (int i = 1; i < argc; i++)
    {
        if (std::strcmp(argv[i], "-d") == 0)
            brls::Logger::setLogLevel(brls::LogLevel::LOG_DEBUG);
        else if (std::strcmp(argv[i], "-v") == 0)
            brls::Application::enableDebuggingView(true);
    }

    brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_AUTO;

    if (!brls::Application::init())
    {
        brls::Logger::error("Unable to init Borealis application");
        return EXIT_FAILURE;
    }

    brls::Application::createWindow("hpak");
    brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
    brls::Application::getPlatform()->getInputManager()->setPadMouseEnabled(true);
    brls::Application::setGlobalQuit(false);

    brls::Application::getExitEvent()->subscribe([]() {
        brls::Logger::info("hpak: exitEvent shutdown begin");
        StoreManager::instance().shutdown();
        util::shutdownIconLoader();
        brls::Logger::info("hpak: exitEvent shutdown done");
    });

    brls::Application::registerXMLView("HomeTab", HomeTab::create);
    brls::Application::registerXMLView("AppsTab", AppsTab::create);
    brls::Application::registerXMLView("InstalledTab", InstalledTab::create);
    brls::Application::registerXMLView("SourcesTab", SourcesTab::create);
    brls::Application::registerXMLView("UpdatesTab", UpdatesTab::create);

    brls::Application::pushActivity(new MainActivity());

    while (brls::Application::mainLoop())
        ;

    brls::Logger::info("hpak: mainLoop exited, teardown begin");
    StoreManager::instance().teardown();
    brls::Logger::info("hpak: teardown done");

    return EXIT_SUCCESS;
}

#ifdef __WINRT__
#include <borealis/core/main.hpp>
#endif
