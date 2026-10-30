#pragma once
#include "routemine/core.hpp"

namespace routemine {
// Each section is independently checked against the original requested design.
// Equal station intervals denote alternative connections, not extra road length.
struct DesignSection { SearchResult candidate; double first_station_m{},last_station_m{}; };
struct DesignGap { double first_station_m{},last_station_m{}; Vec3 start,end; std::string reason; };
struct DesignReview {
    std::vector<DesignSection> sections;
    std::vector<DesignGap> gaps;
    std::optional<SearchResult> connected;
    std::string report;
};
DesignReview review_surface_corridor(const Terrain&,const SearchResult& corridor,const Parameters& requested,
    const UnitCosts&,const SearchOptions&,const RoadOptions&,std::stop_token stop={},
    ProgressCallback progress={},std::function<void(const DesignReview&)> update={});
// Recheck the owning TIN and original parameters before trusting stored blue geometry.
// Saved estimates and local parameter overrides are not trusted by this check.
bool validate_design_review(const Terrain&,const DesignReview&,const Parameters&);
}
