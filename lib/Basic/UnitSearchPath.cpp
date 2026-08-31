#include "plang/Basic/UnitSearchPath.h"

#include <cstdlib>

namespace plang {

std::vector<std::string> unitSearchPaths(const std::string& ExeDir) {
    std::vector<std::string> Paths;
    if (const char* Override = std::getenv("PLANG_UNIT_DIR"); Override && *Override)
        Paths.emplace_back(Override);
    if (!ExeDir.empty())
        Paths.push_back(ExeDir + "/../lib/plang/units");
#ifdef PLANG_UNIT_DIR
    Paths.emplace_back(PLANG_UNIT_DIR);
#endif
    return Paths;
}

} // namespace plang
