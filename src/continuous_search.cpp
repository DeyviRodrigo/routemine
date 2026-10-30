#include "routemine/continuous_search.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <numbers>
#include <sstream>
#include <stdexcept>

namespace routemine {
namespace {
constexpr double pi=std::numbers::pi;
double distance(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);}
bool same(Vec3 a,Vec3 b){return distance(a,b)<1e-6&&std::abs(a.z-b.z)<1e-6;}
double bearing(Vec3 a,Vec3 b){return std::atan2(b.y-a.y,b.x-a.x);}
std::size_t grow(std::size_t initial,std::size_t cap,std::size_t levels){auto value=std::min(initial,cap);for(std::size_t i=0;i<levels&&value<cap;++i)value+=std::min(value,cap-value);return value;}
double fraction(std::size_t value,std::size_t base){double result=0,scale=1.;while(value){scale/=static_cast<double>(base);result+=scale*static_cast<double>(value%base);value/=base;}return result;}
std::vector<double> stations(std::span<const Vec3> route){std::vector<double> result(route.size());for(std::size_t i=1;i<route.size();++i)result[i]=result[i-1]+distance(route[i-1],route[i]);return result;}
Vec3 at(std::span<const Vec3> route,const std::vector<double>& station,double s){auto found=std::lower_bound(station.begin(),station.end(),s);if(found==station.begin())return route.front();if(found==station.end())return route.back();auto i=static_cast<std::size_t>(found-station.begin());double t=(s-station[i-1])/(station[i]-station[i-1]);return {std::lerp(route[i-1].x,route[i].x,t),std::lerp(route[i-1].y,route[i].y,t),std::lerp(route[i-1].z,route[i].z,t)};}
void rebuild_gaps(DesignReview& review,std::span<const Vec3> corridor,const std::vector<double>& station){
    review.gaps.clear();if(review.connected)return;
    std::vector<std::pair<double,double>> covered;for(const auto& s:review.sections)covered.push_back({s.first_station_m,s.last_station_m});std::sort(covered.begin(),covered.end());double cursor=0;
    auto add=[&](double a,double b){if(b-a>1e-5)review.gaps.push_back({a,b,at(corridor,station,a),at(corridor,station,b),"Conexión pendiente; la búsqueda continúa con los parámetros originales."});};
    for(auto [a,b]:covered){add(cursor,a);cursor=std::max(cursor,b);}add(cursor,station.back());
}
bool equivalent(const DesignSection& a,const DesignSection& b){return std::abs(a.first_station_m-b.first_station_m)<1e-6&&std::abs(a.last_station_m-b.last_station_m)<1e-6&&same(a.candidate.route.front(),b.candidate.route.front())&&same(a.candidate.route.back(),b.candidate.route.back());}
bool identical(const SearchResult& a,const SearchResult& b){return a.route.size()==b.route.size()&&std::equal(a.route.begin(),a.route.end(),b.route.begin(),same);}
SearchResult reverse(SearchResult r){const auto last=r.route.size()-1;std::reverse(r.route.begin(),r.route.end());for(auto& c:r.curves){auto first=last-c.last;c.last=last-c.first;c.first=first;c.sweep_degrees=-c.sweep_degrees;}std::reverse(r.curves.begin(),r.curves.end());r.estimate.reset();return r;}
// These polylines are actual probes against the TIN, including unsuccessful
// grade/curvature candidates. They are exploratory gray geometry only.
std::optional<Vec3> probe(const Terrain& terrain,Vec3 start,double heading,double length,double curvature,const RoadOptions& road,std::stop_token stop){
    std::vector<Vec3> points{start};const auto count=std::clamp<std::size_t>(static_cast<std::size_t>(std::ceil(length/5)),2,64);
    for(std::size_t i=1;i<=count&&!stop.stop_requested();++i){double s=length*static_cast<double>(i)/count;Vec3 q;
        if(std::abs(curvature)<1e-12)q={start.x+s*std::cos(heading),start.y+s*std::sin(heading),0};
        else q={start.x+(std::sin(heading+curvature*s)-std::sin(heading))/curvature,start.y+(std::cos(heading)-std::cos(heading+curvature*s))/curvature,0};
        auto z=terrain.elevation(q.x,q.y);if(!z)break;q.z=*z;points.push_back(q);
    }
    if(points.size()>1){if(road.trace)road.trace(points,false);return points.back();}return {};
}
}

DesignReview explore_design_continuously(const Terrain& terrain,const SearchResult& corridor,const Parameters& p,const UnitCosts& costs,
    const SearchOptions& resources,const RoadOptions& options,std::stop_token stop,std::function<void(const DesignReview&,std::size_t)> update){
    (void)measure(corridor.route);const auto station=stations(corridor.route);const auto total=station.back();
    auto road=options;road.adaptive_resolution=false;road.propose_earthwork=false;road.first_feasible=true;road.max_ramp_candidates=std::min<std::size_t>(options.max_ramp_candidates,12);
    if(road.trace)road.trace=[trace=road.trace](std::span<const Vec3> path,bool){trace(path,false);};
    DesignReview review=review_surface_corridor(terrain,corridor,p,costs,resources,road,stop,{},[&](const DesignReview& seed){if(update)update(seed,0);});
    const double minimum=std::max({p.min_straight_m,2*p.min_radius_m,4*p.road_width_m,40.});
    auto publish=[&](std::size_t round){std::ostringstream text;text<<"Exploración continua: ronda "<<round<<". "<<review.sections.size()<<" tramos conformes y "<<review.gaps.size()<<" intervalos pendientes.\n"
        <<"Se conservan pendiente "<<p.max_grade_percent<<" %, radio "<<p.min_radius_m<<" m, recta "<<p.min_straight_m<<" m y separación "<<p.road_width_m<<" m.\n"
        <<(stop.stop_requested()?"Búsqueda detenida por el usuario; resultados conservados.\n":"La búsqueda sigue activa hasta pulsar Cancelar; se exploran nuevas direcciones, ventanas y sectores.\n");review.report=text.str();if(update)update(review,round);};
    auto accept=[&](SearchResult candidate,double first,double last){
        if(candidate.status!=SearchStatus::found||candidate.route.size()<2||!candidate.local_sections.empty())return false;
        if(!same(candidate.route.front(),at(corridor.route,station,first))||!same(candidate.route.back(),at(corridor.route,station,last)))return false;
        try{candidate.estimate=estimate_cost(candidate.route,terrain,p,costs,candidate.curves);}catch(const std::exception&){return false;}
        if(candidate.estimate->validation.metrics.horizontal_m+1e-6<minimum&&last-first<total-1e-6)return false;
        DesignSection incoming{candidate,first,last};for(const auto& old:review.sections)if(equivalent(old,incoming)&&identical(old.candidate,candidate))return false;
        DesignReview next=review;bool complete=first<1e-6&&last>total-1e-6;
        if(complete&&(!next.connected||candidate.estimate->validation.metrics.horizontal_m<measure(next.connected->route).horizontal_m-1e-6))next.connected=candidate;
        // Keep the four shortest alternatives for a fixed connection. This
        // bounds memory while keeping every independent previously found region.
        std::vector<std::size_t> group;for(std::size_t i=0;i<next.sections.size();++i)if(equivalent(next.sections[i],incoming))group.push_back(i);
        if(group.size()>=4){auto worst=*std::max_element(group.begin(),group.end(),[&](auto a,auto b){return measure(next.sections[a].candidate.route).horizontal_m<measure(next.sections[b].candidate.route).horizontal_m;});
            if(measure(next.sections[worst].candidate.route).horizontal_m<=candidate.estimate->validation.metrics.horizontal_m+1e-6){if(next.connected&&(!review.connected||measure(next.connected->route).horizontal_m<measure(review.connected->route).horizontal_m-1e-6)){rebuild_gaps(next,corridor.route,station);if(validate_design_review(terrain,next,p)){review=std::move(next);return true;}}return false;}
            next.sections.erase(next.sections.begin()+static_cast<std::ptrdiff_t>(worst));
        }else if(next.sections.size()>=128)return false;
        next.sections.push_back(std::move(incoming));rebuild_gaps(next,corridor.route,station);
        if(!validate_design_review(terrain,next,p))return false;review=std::move(next);return true;
    };
    const auto terrain_bounds=terrain.bounds();const double extent=distance(terrain_bounds[0],terrain_bounds[1]);
    auto limits=resources;limits.max_states=std::min<std::size_t>(resources.max_states,6000);limits.max_expansions=std::min<std::size_t>(resources.max_expansions,700);limits.max_edges=std::min<std::size_t>(resources.max_edges,14000);
    auto search=[&](Vec3 a,Vec3 b,RoadOptions pose)->SearchResult{if(stop.stop_requested())return {};try{return plan_continuous_ramp(terrain,a,b,p,costs,limits,pose,stop);}catch(const std::exception&){return {};}};
    std::condition_variable_any wake;std::mutex wait_mutex;std::size_t round=0;
    while(!stop.stop_requested()){
        ++round;
        // Increase bounded effort after each eight rounds, up to exactly the
        // user's configured resources. Hard alignments are not permanently
        // restricted to the inexpensive first-round search lattice.
        const auto level=(round-1)/8;
        limits.max_states=grow(6000,resources.max_states,level);
        limits.max_expansions=grow(700,resources.max_expansions,level);
        limits.max_edges=grow(14000,resources.max_edges,level);
        road.max_ramp_candidates=grow(12,options.max_ramp_candidates,level);
        limits.xy_spacing_m=std::max(0.5,resources.xy_spacing_m*(round%3==0?0.45:round%3==1?1.:0.7));road.heading_step_degrees=std::max(1.,options.heading_step_degrees/(1.+static_cast<double>(round%3)));
        // Low-discrepancy angular samples cover all sides instead of repeatedly
        // restarting the same pose. Both A and B contribute actual TIN probes.
        double angle=2*pi*fraction(round,2),radius=std::max(minimum,extent*(0.03+0.45*fraction(round,3)));
        std::vector<Vec3> probed_waypoints;
        for(auto endpoint:{corridor.route.front(),corridor.route.back(),at(corridor.route,station,total*fraction(round,7))})for(double turn:{-1.,0.,1.})
            if(auto reached=probe(terrain,endpoint,angle,radius,turn/std::max(1.,p.min_radius_m),road,stop))probed_waypoints.push_back(*reached);
        double first=0,last=total;
        if(round%4!=0){
            if(!review.gaps.empty()&&round%3!=0){const auto& gap=review.gaps[(round-1)%review.gaps.size()];double buffer=std::max(p.road_width_m,1.);first=gap.first_station_m+buffer;last=gap.last_station_m-buffer;
                if(last-first>minimum*2){double width=minimum*(1.+3*fraction(round,5));width=std::min(width,last-first);first+=(last-first-width)*fraction(round,7);last=first+width;}
            }else if(!review.sections.empty()&&round%3==0){const auto& section=review.sections[(round-1)%review.sections.size()];first=section.first_station_m;last=section.last_station_m;}
            else{double width=std::min(total,minimum*(1.+6*fraction(round,5)));first=(total-width)*fraction(round,7);last=first+width;}
        }
        if(last-first>=minimum-1e-6||round%4==0){
            auto a=at(corridor.route,station,first),b=at(corridor.route,station,last);const bool backwards=round%2==0;
            auto pose=road;pose.start_heading.reset();pose.finish_heading=bearing(a,b)+(round%3==0?(round%2?1.:-1.)*pi/4:0.);
            if(backwards){std::swap(a,b);pose.finish_heading=bearing(a,b);}
            auto direct=search(a,b,pose);if(direct.status==SearchStatus::found){if(backwards)direct=reverse(std::move(direct));accept(std::move(direct),first,last);}
            // An interior waypoint moves the search to a different side of the
            // terrain. Two searches share one tangent pose; the complete joined
            // geometry is checked again before any blue update is accepted.
            // Use a reached endpoint of an actual TIN probe as the search
            // waypoint: the displayed sector is an input to route generation,
            // rather than a decorative ray unrelated to the solver.
            std::optional<Vec3> waypoint_candidate;
            for(std::size_t k=0;k<probed_waypoints.size();++k){auto reached=probed_waypoints[(round+k)%probed_waypoints.size()];if(distance(a,reached)>minimum/2&&distance(b,reached)>minimum/2){waypoint_candidate=reached;break;}}
            if(waypoint_candidate&&!stop.stop_requested()){auto waypoint=*waypoint_candidate;
                const double tangent=bearing(a,b)+(round%2?pi/2:-pi/2);auto entry=road;entry.start_heading.reset();entry.finish_heading=tangent;
                probe(terrain,waypoint,tangent,radius,0,road,stop);
                auto left=search(a,waypoint,entry);if(left.status==SearchStatus::found&&!stop.stop_requested()){
                    auto exit=road;exit.start_heading=tangent;exit.finish_heading.reset();auto right=search(waypoint,b,exit);
                    if(right.status==SearchStatus::found){auto joined=std::move(left);auto offset=joined.route.size()-1;joined.route.insert(joined.route.end(),right.route.begin()+1,right.route.end());for(auto c:right.curves){c.first+=offset;c.last+=offset;joined.curves.push_back(c);}joined.estimate.reset();
                        if(backwards)joined=reverse(std::move(joined));accept(std::move(joined),first,last);}
                }
            }
        }
        publish(round);
        if(!stop.stop_requested()){std::unique_lock lock(wait_mutex);wake.wait_for(lock,stop,std::chrono::milliseconds(100),[]{return false;});}
    }
    if(!round||!stop.stop_requested())publish(round);else{std::ostringstream text;text<<review.report<<"Exploración finalizada por Cancelar.\n";review.report=text.str();}
    return review;
}
}
