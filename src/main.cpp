#include <iostream>
#include "compat/compat.hpp"

int main() {
    std::cout << "hpak (PC dev build)\n";
    std::cout << "Root: " << compat::GetRootFilePath() << "\n";
    std::cout << "Index: " << compat::GetIndexFilePath() << "\n";
    return 0;
}
