#include "tab/options_tab.hpp"

#include "store/store.hpp"
#include "util/settings.hpp"

OptionsTab::OptionsTab()
{
    this->inflateFromXMLRes("xml/tabs/options_tab.xml");

    auto& settings = HpSettings::instance();
    auto& store = StoreManager::instance();
    store.init();

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
    store.init();

    if (on)
    {
        for (auto& d : StoreManager::getDefaultRepos())
        {
            if (d.url.empty())
                continue;
            bool has = false;
            for (auto& r : store.getRepos())
            {
                if (r.url == d.url)
                {
                    has = true;
                    break;
                }
            }
            if (!has)
                store.addRepo(d.url);
        }
    }
    else
    {
        for (auto& d : StoreManager::getDefaultRepos())
        {
            if (!d.url.empty())
                store.removeRepo(d.url);
        }
    }
}

brls::View* OptionsTab::create()
{
    return new OptionsTab();
}
