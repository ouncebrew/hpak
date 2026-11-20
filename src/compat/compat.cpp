#include "compat.hpp"
#include <string>

namespace compat {
    std::string GetRootFilePath() {
        #ifdef __SWITCH__
        return hos::GetRootFilePath();
        #else
        return pc::GetRootFilePath();
        #endif
    }
    std::string GetIndexFilePath() {
        #ifdef __SWITCH__
        return hos::GetIndexFilePath();
        #else
        return pc::GetIndexFilePath();
        #endif
    }
}