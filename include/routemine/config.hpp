#pragma once
#include "routemine/core.hpp"
#include <map>
#include <ostream>
namespace routemine {
using Settings=std::map<std::string,std::string>;
struct Configuration {
    Vec3 start,finish;
    std::string terrain_path;
    std::string road_profile{"mining_haul_provisional"};
    std::string vehicle_profile{"haul_truck_unspecified"};
    std::string cost_profile{"synthetic_example"};
    Parameters parameters;
    UnitCosts costs;
    SearchOptions search;
    std::vector<std::string> warnings;
};
Settings read_settings(std::istream&);
// Explicit values > selected profile > central defaults. Legacy requires all old keys.
Configuration resolve_configuration(const Terrain&,std::string terrain_path,Settings,bool legacy=false);
void write_configuration(std::ostream&,const Configuration&);
}
