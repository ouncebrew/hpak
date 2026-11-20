#include "hos.hpp"
#include <string>

namespace compat::hos {
    std::string GetRootFilePath() {
        return "sdmc:/";
    }
    std::string GetIndexFilePath() {
        return GetRootFilePath() + "hpak/index.json";
    }
}