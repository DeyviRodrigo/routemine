#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <queue>
#include <stdexcept>
namespace routemine {
SearchResult plan_cpu(const Terrain& terrain,Vec3 start,Vec3 finish,const Parameters& p,
                      const UnitCosts& costs,const SearchOptions& options,std::stop_token stop) {
    if(stop.stop_requested()){SearchResult r;r.status=SearchStatus::cancelled;return r;}
    if(alignment_enabled(p)) {
        RoadOptions road;road.propose_earthwork=false;
        return plan_road(terrain,start,finish,p,costs,options,road,stop).surface;
    }
    if(!finite(start)||!finite(finish))throw std::invalid_argument("Invalid endpoints");
    (void)measure(std::vector<Vec3>{start,finish});
    if(costs.reusable_cut_fraction!=0)throw std::invalid_argument("Search requires reusable_cut_fraction=0 for additive costs");
    for(double v:{options.xy_spacing_m,options.z_min,options.z_max,options.z_step_m})
        if(!std::isfinite(v))throw std::invalid_argument("Invalid lattice");
    if(options.xy_spacing_m<=0||options.z_step_m<=0||options.z_min>options.z_max||options.max_states<2||options.max_expansions<1||options.max_edges<1)
        throw std::invalid_argument("Invalid lattice/budget");
    auto bounds=terrain.bounds();
    auto in_bounds=[&](Vec3 q){return q.x>=bounds[0].x && q.x<=bounds[1].x && q.y>=bounds[0].y && q.y<=bounds[1].y
        &&q.z>=options.z_min&&q.z<=options.z_max;};
    if(!in_bounds(start)||!in_bounds(finish))throw std::invalid_argument("Endpoints outside lattice bounds");
    auto axis=[&](double lo,double hi,double step,double a,double b){
        double intervals=std::ceil((hi-lo)/step);
        if(!std::isfinite(intervals)||intervals>static_cast<double>(options.max_states))throw std::length_error("Lattice axis exceeds state budget");
        std::vector<double> v{lo,hi,a,b};
        for(std::size_t i=1;i<static_cast<std::size_t>(intervals);++i)v.push_back(lo+static_cast<double>(i)*step);
        std::sort(v.begin(),v.end());v.erase(std::unique(v.begin(),v.end()),v.end());return v;
    };
    auto xs=axis(bounds[0].x,bounds[1].x,options.xy_spacing_m,start.x,finish.x);
    auto ys=axis(bounds[0].y,bounds[1].y,options.xy_spacing_m,start.y,finish.y);
    auto zs=axis(options.z_min,options.z_max,options.z_step_m,start.z,finish.z);
    if(xs.size()>options.max_states/ys.size() || xs.size()*ys.size()>options.max_states/zs.size())
        throw std::length_error("Lattice exceeds max_states");
    const auto nx=xs.size(),ny=ys.size(),nz=zs.size(),total=nx*ny*nz;
    auto index=[&](std::size_t x,std::size_t y,std::size_t z){return (y*nx+x)*nz+z;};
    auto coord=[&](std::size_t id){return Vec3{xs[(id/nz)%nx],ys[id/(nz*nx)],zs[id%nz]};};
    auto find=[](const auto& v,double x){return static_cast<std::size_t>(std::lower_bound(v.begin(),v.end(),x)-v.begin());};
    auto first=index(find(xs,start.x),find(ys,start.y),find(zs,start.z));
    auto last=index(find(xs,finish.x),find(ys,finish.y),find(zs,finish.z));
    // Validate cost/parameter contracts before exploring any edge.
    Parameters contract=p;
    // Reject bad numerical input via the common evaluator; geometry violations
    // of the direct segment are expected and do not disqualify all alternatives.
    (void)validate(std::vector<Vec3>{start,finish},terrain,contract);
    for(double v:{costs.road_per_m2,costs.soil_cut_per_m3,costs.rock_cut_per_m3,costs.fill_placement_per_m3,
        costs.imported_material_per_m3,costs.disposal_per_m3,costs.haul_per_m3_km,costs.rock_fraction,
        costs.internal_haul_km,costs.import_haul_km,costs.disposal_haul_km})
        if(!std::isfinite(v)||v<0)throw std::invalid_argument("Invalid search cost");
    if(costs.rock_fraction>1||p.road_width_m<=0)throw std::invalid_argument("Invalid search fraction/width");
    const auto none=std::numeric_limits<std::size_t>::max();
    std::vector<double> distance(total,std::numeric_limits<double>::infinity());
    std::vector<std::size_t> previous(total,none);
    using Entry=std::pair<double,std::size_t>;
    std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    distance[first]=0;queue.emplace(0,first);SearchResult result;
    while(!queue.empty()) {
        if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
        auto [cost,id]=queue.top();queue.pop();if(cost!=distance[id])continue;
        if(id==last){
            for(auto cursor=last;cursor!=none;cursor=previous[cursor])result.route.push_back(coord(cursor));
            std::reverse(result.route.begin(),result.route.end());
            result.estimate=estimate_cost(result.route,terrain,p,costs);
            result.status=SearchStatus::found;return result;
        }
        if(result.expanded>=options.max_expansions){result.status=SearchStatus::budget_exceeded;return result;}
        ++result.expanded;
        auto a=coord(id);const auto ix=(id/nz)%nx,iy=id/(nz*nx);
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
            if(dx==0&&dy==0)continue;
            const auto xx=static_cast<long long>(ix)+dx,yy=static_cast<long long>(iy)+dy;
            if(xx<0||yy<0||static_cast<std::size_t>(xx)>=nx||static_cast<std::size_t>(yy)>=ny)continue;
            double h=std::hypot(xs[static_cast<std::size_t>(xx)]-a.x,ys[static_cast<std::size_t>(yy)]-a.y);
            double rise=h*p.max_grade_percent/100.0+1e-10;
            auto begin=std::lower_bound(zs.begin(),zs.end(),a.z-rise),end=std::upper_bound(zs.begin(),zs.end(),a.z+rise);
            for(auto it=begin;it!=end;++it){
                if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
                if(result.edges_checked>=options.max_edges){result.status=SearchStatus::budget_exceeded;return result;}
                auto next=index(static_cast<std::size_t>(xx),static_cast<std::size_t>(yy),static_cast<std::size_t>(it-zs.begin()));
                ++result.edges_checked;double edge;
                try {edge=estimate_cost(std::vector<Vec3>{a,coord(next)},terrain,p,costs).total;}
                catch(const std::invalid_argument&){continue;}
                const double candidate=cost+edge;
                if(!std::isfinite(candidate))throw std::overflow_error("Search cost overflow");
                if(candidate<distance[next]){distance[next]=candidate;previous[next]=id;queue.emplace(candidate,next);}
            }
        }
    }
    return result;
}
}
