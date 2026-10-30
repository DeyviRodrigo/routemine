#pragma once
#include "routemine/core.hpp"
#include "routemine/road_standards.hpp"
#include <map>
#include <ostream>
namespace routemine {
using Settings=std::map<std::string,std::string>;
// Initial desktop settings derived from the active local RouteTec monolith.
// Existing CLI defaults remain compatible with saved requests.
Settings routetec_interface_defaults();
struct Configuration {
    Vec3 start,finish;
    std::string terrain_path;
    std::string road_profile{"mining_haul_provisional"};
    std::string vehicle_profile{"haul_truck_unspecified"};
    std::string cost_profile{"synthetic_example"};
    std::string standards_mode{"custom"};
    RoadStandardInput design_context;
    bool design_altitude_auto{true};
    Parameters parameters;
    UnitCosts costs;
    SearchOptions search;
    RoadOptions road_search;
    std::vector<std::string> warnings;
};
Settings read_settings(std::istream&);
// Explicit values > selected profile > central defaults. Legacy requires all old keys.
Configuration resolve_configuration(const Terrain&,std::string terrain_path,Settings,bool legacy=false);
void write_configuration(std::ostream&,const Configuration&);
}
