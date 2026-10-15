#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace routemine {
Validation validate(std::span<const Vec3> route,const Terrain& terrain,const Parameters& p) {
    for (double value:{p.max_grade_percent,p.max_cut_m,p.max_fill_m,p.min_radius_m,p.road_width_m,p.min_straight_m,p.sample_spacing_m,p.surface_tolerance_m})
        if(!std::isfinite(value)||value<0) throw std::invalid_argument("Invalid nonnegative parameter");
    if(p.max_grade_percent<=0 || p.max_grade_percent>100 || p.sample_spacing_m<=0 || p.max_samples<2)
        throw std::invalid_argument("Invalid grade or sampling limits");
    if(p.mode!=DesignMode::surface && p.mode!=DesignMode::earthwork) throw std::invalid_argument("Invalid mode");
    Validation r; r.metrics=measure(route);
    if(r.metrics.max_grade_percent>p.max_grade_percent+1e-8) r.violations.push_back("grade_limit");
    bool missing=false, cut=false, fill=false, off_surface=false;
    auto check_depth=[&](double projected,double ground){
        const double delta=projected-ground;
        r.max_cut_m=std::max(r.max_cut_m,-delta);r.max_fill_m=std::max(r.max_fill_m,delta);
        cut|=(-delta>p.max_cut_m+1e-8);fill|=(delta>p.max_fill_m+1e-8);
        off_surface|=(std::abs(delta)>p.surface_tolerance_m);
    };
    for(std::size_t i=1;i<route.size();++i) {
        auto a=route[i-1], b=route[i];
        const double length=std::hypot(b.x-a.x,b.y-a.y);
        auto profile=terrain.segment_profile(a,b);
        if(!profile)missing=true;
        else for(auto ground:*profile){
            if(r.samples>=p.max_samples)throw std::length_error("Face-boundary sampling budget exceeded");
            ++r.samples;
            const double t=std::hypot(ground.x-a.x,ground.y-a.y)/length;
            check_depth(std::lerp(a.z,b.z,std::clamp(t,0.0,1.0)),ground.z);
        }
        const double steps=std::max(1.0,std::ceil(length/p.sample_spacing_m));
        if(!std::isfinite(steps) || steps>=static_cast<double>(p.max_samples-r.samples))
            throw std::length_error("Sampling budget exceeded; choose an explicit coarser spacing");
        const auto count=static_cast<std::size_t>(steps);
        for(std::size_t j=0;j<=count;++j) {
            const double t=static_cast<double>(j)/static_cast<double>(count);
            const Vec3 q{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
            ++r.samples;
            auto z=terrain.elevation(q.x,q.y);
            if(!z) {missing=true;continue;}
            check_depth(q.z,*z);
        }
    }
    if(missing) r.violations.push_back("terrain_missing_or_ambiguous");
    if(p.mode==DesignMode::surface) { if(off_surface) r.violations.push_back("surface_separation"); }
    else {if(cut) r.violations.push_back("cut_limit"); if(fill) r.violations.push_back("fill_limit");}
    // These need an alignment with arc/tangent primitives and a swept corridor.
    r.not_evaluated={"curve_radius","straight_before_turn","road_width_clearance","zero_grade_on_curves","self_intersection"};
    return r;
}
}
