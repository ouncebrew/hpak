#include "pc.hpp"
#include <string>

namespace compat::pc {
    std::string GetRootFilePath() {
        return "/";
    }
    std::string GetIndexFilePath() {
        return GetRootFilePath() + "hpak/index.json";
    }
}