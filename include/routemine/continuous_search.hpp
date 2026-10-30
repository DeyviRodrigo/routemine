#pragma once
#include "routemine/design_review.hpp"

namespace routemine {
// Runs until stop is requested, including after finding a complete connection.
// Every attempt is bounded, but there is no total-round limit. The callback
// publishes the accumulated independently validated geometry once per round.
DesignReview explore_design_continuously(const Terrain&,const SearchResult& gray_corridor,
    const Parameters& requested,const UnitCosts&,const SearchOptions&,const RoadOptions&,
    std::stop_token stop,std::function<void(const DesignReview&,std::size_t round)> update={});
}
