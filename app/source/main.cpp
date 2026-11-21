#include <borealis.hpp>
#include <cstdlib>

#include "activity/main_activity.hpp"
#include "tab/apps_tab.hpp"
#include "tab/home_tab.hpp"
#include "tab/installed_tab.hpp"
#include "tab/sources_tab.hpp"

#if defined(__PSV__) && defined(BOREALIS_USE_OPENGL)
extern "C" unsigned int sceLibcHeapSize = 2 * 1024 * 1024;
#endif

int main(int argc, char* argv[])
{
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
    brls::Application::setGlobalQuit(false);

    brls::Application::registerXMLView("HomeTab", HomeTab::create);
    brls::Application::registerXMLView("AppsTab", AppsTab::create);
    brls::Application::registerXMLView("InstalledTab", InstalledTab::create);
    brls::Application::registerXMLView("SourcesTab", SourcesTab::create);

    brls::Application::pushActivity(new MainActivity());

    while (brls::Application::mainLoop())
        ;

    return EXIT_SUCCESS;
}

#ifdef __WINRT__
#include <borealis/core/main.hpp>
#endif
