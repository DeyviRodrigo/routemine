#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace routemine {
CostEstimate estimate_cost(std::span<const Vec3> route,const Terrain& terrain,
                           const Parameters& p,const UnitCosts& c) {
    for(double value:{c.road_per_m2,c.soil_cut_per_m3,c.rock_cut_per_m3,c.fill_placement_per_m3,
        c.imported_material_per_m3,c.disposal_per_m3,c.haul_per_m3_km,c.rock_fraction,
        c.reusable_cut_fraction,c.internal_haul_km,c.import_haul_km,c.disposal_haul_km})
        if(!std::isfinite(value)||value<0) throw std::invalid_argument("Invalid cost input");
    if(c.rock_fraction>1||c.reusable_cut_fraction>1||p.road_width_m<=0)
        throw std::invalid_argument("Invalid fractions or road width");
    CostEstimate result; result.validation=validate(route,terrain,p);
    if(!result.validation.checked_constraints_pass())
        throw std::invalid_argument("Candidate fails grade/terrain/earthwork constraints");
    auto accumulate=[&](double a,double b,double length){
        // Signed depth = terrain - projected profile. Split cut/fill crossover.
        if(a>=0 && b>=0) result.cut_m3+=(a+b)*0.5*length*p.road_width_m;
        else if(a<=0 && b<=0) result.fill_m3+=(-a-b)*0.5*length*p.road_width_m;
        else {
            double first=length*std::abs(a)/(std::abs(a)+std::abs(b));
            if(a>0){result.cut_m3+=a*0.5*first*p.road_width_m;result.fill_m3+=-b*0.5*(length-first)*p.road_width_m;}
            else {result.fill_m3+=-a*0.5*first*p.road_width_m;result.cut_m3+=b*0.5*(length-first)*p.road_width_m;}
        }
    };
    for(std::size_t i=1;i<route.size();++i){
        auto a=route[i-1],b=route[i];double h=std::hypot(b.x-a.x,b.y-a.y);
        auto profile=terrain.segment_profile(a,b);
        if(!profile)throw std::invalid_argument("Missing terrain in cost estimate");
        double previous=0,previous_t=0;
        for(std::size_t j=0;j<profile->size();++j){
            auto q=(*profile)[j];
            double t=std::clamp(std::hypot(q.x-a.x,q.y-a.y)/h,0.0,1.0);
            double depth=q.z-std::lerp(a.z,b.z,t);
            if(j)accumulate(previous,depth,(t-previous_t)*h);
            previous=depth;previous_t=t;
        }
    }
    result.reused_m3=std::min(result.fill_m3,result.cut_m3*c.reusable_cut_fraction);
    result.imported_m3=result.fill_m3-result.reused_m3;
    result.surplus_m3=result.cut_m3-result.reused_m3;
    result.road=result.validation.metrics.horizontal_m*p.road_width_m*c.road_per_m2;
    result.excavation=result.cut_m3*((1-c.rock_fraction)*c.soil_cut_per_m3+c.rock_fraction*c.rock_cut_per_m3);
    result.fill_placement=result.fill_m3*c.fill_placement_per_m3;
    result.material=result.imported_m3*c.imported_material_per_m3;
    result.disposal=result.surplus_m3*c.disposal_per_m3;
    result.haul=c.haul_per_m3_km*(result.reused_m3*c.internal_haul_km+result.imported_m3*c.import_haul_km+result.surplus_m3*c.disposal_haul_km);
    result.total=result.road+result.excavation+result.fill_placement+result.material+result.disposal+result.haul;
    if(!std::isfinite(result.total)||!std::isfinite(result.cut_m3)||!std::isfinite(result.fill_m3))
        throw std::overflow_error("Cost estimate overflow");
    return result;
}
}
