#include "tab/options_tab.hpp"

#include "store/store.hpp"
#include "util/settings.hpp"

OptionsTab::OptionsTab()
{
    this->inflateFromXMLRes("xml/tabs/options_tab.xml");

    auto& settings = HpSettings::instance();
    auto& store = StoreManager::instance();
    // Async warm-up: never block the UI thread on disk/network init here.
    // The toggle below only reads the cache and queues background repo ops.
    store.ensureInitializedAsync();

    optRepos->init("Default sources", settings.addDefaultRepos, [this](bool value) {
        auto& s = HpSettings::instance();
        s.addDefaultRepos = value;
        s.save();
        syncDefaultReposToggle(value);
    });

    optMouse->init("Joy-Con 2 mouse mode", settings.mouseMode, [](bool value) {
        auto& s = HpSettings::instance();
        s.mouseMode = value;
        s.save();
        brls::Application::getPlatform()->getInputManager()->setPadMouseEnabled(value);
    });

    optLogging->init("File logging", settings.fileLogging, [](bool value) {
        auto& s = HpSettings::instance();
        s.fileLogging = value;
        s.save();
    });
}

void OptionsTab::syncDefaultReposToggle(bool on)
{
    auto& store = StoreManager::instance();
    // addRepo/removeRepo run on background threads and publish to the cache;
    // getRepos() here is a non-blocking cache read.
    store.ensureInitializedAsync([on]() {
        auto& s = StoreManager::instance();
        if (on)
        {
            for (auto& d : StoreManager::getDefaultRepos())
            {
                if (d.url.empty())
                    continue;
                bool has = false;
                for (auto& r : s.getRepos())
                {
                    if (r.url == d.url)
                    {
                        has = true;
                        break;
                    }
                }
                if (!has)
                    s.addRepo(d.url);
            }
        }
        else
        {
            for (auto& d : StoreManager::getDefaultRepos())
            {
                if (!d.url.empty())
                    s.removeRepo(d.url);
            }
        }
    });
}

brls::View* OptionsTab::create()
{
    return new OptionsTab();
}
