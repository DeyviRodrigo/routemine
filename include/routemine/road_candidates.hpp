#pragma once
#include "routemine/design_review.hpp"
namespace routemine {
struct RoadCandidateLimits {double max_intervention_length_m{100};double max_surface_grade_factor{2};};
// A road candidate always respects the road's requested grade/radius/depths.
// The factor applies only to the underlying terrain, never to the road profile.
bool validate_road_candidate(const Terrain&,const SearchResult&,const Parameters&,const RoadCandidateLimits&);
// Gray exploration checks terrain coverage and longitudinal grade only.
// It does not certify road geometry, width, or earthwork limits.
bool validate_grade_trace(const Terrain&,std::span<const Vec3>,double max_grade_percent,std::size_t max_samples=1000000);
DesignReview explore_road_candidates(const Terrain&,const SearchResult& reference,const Parameters&,const UnitCosts&,
    const SearchOptions&,const RoadOptions&,const RoadCandidateLimits&,std::stop_token,
    std::function<void(const DesignReview&,std::size_t)> update={},
    std::function<void(const SearchResult&)> branch={},
    std::function<void(std::span<const Vec3>)> exploratory={});
}
