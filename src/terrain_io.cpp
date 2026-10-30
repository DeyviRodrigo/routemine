#include "routemine/core.hpp"
#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <utility>
namespace routemine {
Terrain load_terrain(const std::filesystem::path& path,DxfImportReport* report,ProgressCallback progress) {
    if(report)*report={};
    std::ifstream input(path);if(!input)throw std::runtime_error("Cannot open terrain file");
    auto extension=path.extension().string();
    std::transform(extension.begin(),extension.end(),extension.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if(extension==".obj")return load_obj(input,std::move(progress));
    if(extension==".dxf")return load_dxf(input,report,std::move(progress));
    throw std::invalid_argument("Terrain format must be .dxf (ASCII 3DFACE) or .obj (triangles)");
}
}
