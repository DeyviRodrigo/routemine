#pragma once
#include <optional>
#include <string>
#include <vector>

namespace routemine {

enum class DesignTerrain { flat, rolling, rugged, steep };

// DG-2018 guidance for a third-class road. This is separate from effective
// search Parameters: a recommendation never changes the user's constraints.
struct RoadStandardInput {
    double design_speed_kmh{20};
    DesignTerrain terrain{DesignTerrain::steep};
    double altitude_m{};
    double superelevation_percent{6};
    bool ice_risk{true};
};

struct RoadStandardRecommendations {
    double design_speed_kmh{};
    double lateral_friction{};
    double min_radius_m{};
    // S: a tangent between opposite-direction curves; O: the other tangents
    // covered by the DG table. Neither value is an arc length.
    double min_straight_s_m{}, min_straight_o_m{};
    std::optional<double> max_grade_percent;
    std::optional<double> base_road_width_m;
    // True only for a speed/orography combination listed for third-class roads.
    bool tabulated_speed{};
    std::string profile_label;
    std::vector<std::string> diagnostics;
    std::vector<std::string> source_references;
};

// Speeds below 30 use the explicitly stated "30 or less" friction coefficient;
// other supported speeds follow the verified discrete DG friction tables.
// A computed radius is not a tabulated radius or a vehicle swept-path check.
RoadStandardRecommendations recommend_road_standards(const RoadStandardInput&);

} // namespace routemine
