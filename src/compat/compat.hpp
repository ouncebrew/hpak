#pragma once
#ifdef __SWITCH__
#include "hos/hos.hpp"
#else
#include "pc/pc.hpp"
#endif

namespace compat {
    std::string GetRootFilePath();
    std::string GetIndexFilePath();
}