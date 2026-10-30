#include "routemine/design_review.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>

namespace routemine {
namespace {
constexpr double pi=std::numbers::pi,rad=pi/180;
double hdist(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);}
bool same(Vec3 a,Vec3 b){return hdist(a,b)<1e-6&&std::abs(a.z-b.z)<1e-6;}
double heading(Vec3 a,Vec3 b){return std::atan2(b.y-a.y,b.x-a.x);}
double segment_distance(Vec3 p,Vec3 a,Vec3 b){auto dx=b.x-a.x,dy=b.y-a.y;auto den=dx*dx+dy*dy;auto t=den>0?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/den,0.,1.):0.;return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);}
double cross(Vec3 a,Vec3 b,Vec3 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
double edge_distance(Vec3 a,Vec3 b,Vec3 c,Vec3 d){
    if(std::min(a.x,b.x)<=std::max(c.x,d.x)&&std::min(c.x,d.x)<=std::max(a.x,b.x)&&std::min(a.y,b.y)<=std::max(c.y,d.y)&&std::min(c.y,d.y)<=std::max(a.y,b.y)&&
       cross(a,b,c)*cross(a,b,d)<=0&&cross(c,d,a)*cross(c,d,b)<=0)return 0;
    return std::min({segment_distance(a,c,d),segment_distance(b,c,d),segment_distance(c,a,b),segment_distance(d,a,b)});
}
bool separated(const SearchResult& a,const SearchResult& b,double width){
    for(std::size_t i=1;i<a.route.size();++i)for(std::size_t j=1;j<b.route.size();++j){auto p=a.route[i-1],q=a.route[i],r=b.route[j-1],s=b.route[j];
        if(std::max(p.x,q.x)+width<std::min(r.x,s.x)||std::max(r.x,s.x)+width<std::min(p.x,q.x)||std::max(p.y,q.y)+width<std::min(r.y,s.y)||std::max(r.y,s.y)+width<std::min(p.y,q.y))continue;
        if(edge_distance(p,q,r,s)<width-1e-5)return false;
    }return true;
}
bool alternatives(const DesignSection& a,const DesignSection& b){return std::abs(a.first_station_m-b.first_station_m)<1e-6&&std::abs(a.last_station_m-b.last_station_m)<1e-6&&same(a.candidate.route.front(),b.candidate.route.front())&&same(a.candidate.route.back(),b.candidate.route.back());}
std::vector<Vec3> simplify(std::span<const Vec3> raw,double tolerance){
    if(raw.size()<3)return {raw.begin(),raw.end()};
    std::vector<unsigned char> keep(raw.size());keep.front()=keep.back()=1;
    std::vector<std::pair<std::size_t,std::size_t>> work{{0,raw.size()-1}};
    while(!work.empty()){auto [first,last]=work.back();work.pop_back();double maximum=tolerance;std::size_t selected=last;
        for(auto i=first+1;i<last;++i){double d=segment_distance(raw[i],raw[first],raw[last]);if(d>maximum){maximum=d;selected=i;}}
        if(selected!=last){keep[selected]=1;work.push_back({first,selected});work.push_back({selected,last});}}
    std::vector<Vec3> result;for(std::size_t i=0;i<raw.size();++i)if(keep[i])result.push_back(raw[i]);return result;
}
std::optional<SearchResult> fit(const Terrain& terrain,std::span<const Vec3> raw,const Parameters& p,const UnitCosts& costs,double tolerance,std::stop_token stop){
    auto path=simplify(raw,tolerance);if(path.size()<2)return {};
    struct Corner{double turn{},offset{};Vec3 begin,end,center;};std::vector<Corner> corners(path.size());
    for(std::size_t i=1;i+1<path.size();++i){double before=heading(path[i-1],path[i]),after=heading(path[i],path[i+1]);auto& c=corners[i];c.turn=std::remainder(after-before,2*pi);
        if(std::abs(c.turn)<1e-7)continue;
        if(p.min_radius_m<=0||std::abs(c.turn)>=pi-1e-6)return {};
        c.offset=p.min_radius_m*std::tan(std::abs(c.turn)/2);
        c.begin={path[i].x-c.offset*std::cos(before),path[i].y-c.offset*std::sin(before),0};
        c.end={path[i].x+c.offset*std::cos(after),path[i].y+c.offset*std::sin(after),0};
        double sign=std::copysign(1.,c.turn);c.center={c.begin.x-sign*p.min_radius_m*std::sin(before),c.begin.y+sign*p.min_radius_m*std::cos(before),0};
    }
    for(std::size_t i=1;i<path.size();++i)if(corners[i-1].offset+corners[i].offset>hdist(path[i-1],path[i])-1e-7)return {};
    SearchResult result;result.route={path.front()};result.strategy="continuous_ramp";result.effective_xy_spacing_m=p.sample_spacing_m;result.effective_heading_step_degrees=1;
    auto append=[&](Vec3 q){
        if(stop.stop_requested())return false;
        if(hdist(result.route.back(),q)<1e-8)return true;
        auto profile=terrain.segment_profile(result.route.back(),q);if(!profile)return false;
        for(auto g:*profile)if(hdist(result.route.back(),g)>1e-8){if(result.route.size()>=p.max_samples)return false;result.route.push_back(g);}return true;
    };
    for(std::size_t i=1;i+1<path.size();++i){const auto& c=corners[i];if(std::abs(c.turn)<1e-7){if(!append(path[i]))return {};continue;}
        if(!append(c.begin))return {};auto first=result.route.size()-1;
        auto count=static_cast<std::size_t>(std::ceil(std::max(std::abs(c.turn)/rad,p.min_radius_m*std::abs(c.turn)/p.sample_spacing_m)));
        if(count>p.max_samples)return {};double start=heading(c.center,c.begin);
        for(std::size_t j=1;j<=count;++j){double a=start+c.turn*static_cast<double>(j)/count;if(!append(j==count?c.end:Vec3{c.center.x+p.min_radius_m*std::cos(a),c.center.y+p.min_radius_m*std::sin(a),0}))return {};}
        result.curves.push_back({first,result.route.size()-1,c.center,c.turn/rad});
    }
    if(!append(path.back()))return {};result.route.front()=raw.front();result.route.back()=raw.back();
    try{result.estimate=estimate_cost(result.route,terrain,p,costs,result.curves);}catch(const std::exception&){return {};}
    result.status=SearchStatus::found;return result;
}
bool equal_path(const SearchResult& a,const SearchResult& b){return a.route.size()==b.route.size()&&std::equal(a.route.begin(),a.route.end(),b.route.begin(),same);}
std::vector<double> stations(std::span<const Vec3> route){std::vector<double> out(route.size());for(std::size_t i=1;i<route.size();++i)out[i]=out[i-1]+hdist(route[i-1],route[i]);return out;}
Vec3 at(std::span<const Vec3> route,const std::vector<double>& station,double distance){
    auto hi=std::lower_bound(station.begin(),station.end(),distance);if(hi==station.begin())return route.front();if(hi==station.end())return route.back();auto i=static_cast<std::size_t>(hi-station.begin());double t=(distance-station[i-1])/(station[i]-station[i-1]);return {std::lerp(route[i-1].x,route[i].x,t),std::lerp(route[i-1].y,route[i].y,t),std::lerp(route[i-1].z,route[i].z,t)};
}
void gaps(DesignReview& review,std::span<const Vec3> corridor,const std::vector<double>& station){
    review.gaps.clear();if(review.connected)return;
    std::vector<std::pair<double,double>> covered;for(const auto& section:review.sections)covered.push_back({section.first_station_m,section.last_station_m});std::sort(covered.begin(),covered.end());double previous=0;
    auto add=[&](double a,double b){if(b-a>1e-5)review.gaps.push_back({a,b,at(corridor,station,a),at(corridor,station,b),"Conexión pendiente: no se encontró un trazado que cumpla los parámetros originales en este intervalo."});};
    for(auto [a,b]:covered){add(previous,a);previous=std::max(previous,b);}add(previous,station.back());
}
bool checked(const Terrain& terrain,const SearchResult& candidate,const Parameters& p){
    if(candidate.status!=SearchStatus::found||candidate.route.size()<2||!candidate.local_sections.empty())return false;
    try{return validate(candidate.route,terrain,p,candidate.curves).checked_constraints_pass();}catch(const std::exception&){return false;}
}
}

bool validate_design_review(const Terrain& terrain,const DesignReview& review,const Parameters& p){
    if(review.sections.size()>128||review.gaps.size()>512)return false;
    for(const auto& section:review.sections){if(!std::isfinite(section.first_station_m)||!std::isfinite(section.last_station_m)||section.first_station_m<0||section.last_station_m<=section.first_station_m||!checked(terrain,section.candidate,p))return false;}
    if(review.connected&&!checked(terrain,*review.connected,p))return false;
    auto complete=[&](const DesignSection& section){return review.connected&&same(section.candidate.route.front(),review.connected->route.front())&&same(section.candidate.route.back(),review.connected->route.back());};
    for(std::size_t i=0;i<review.sections.size();++i)for(std::size_t j=i+1;j<review.sections.size();++j){const auto& a=review.sections[i];const auto& b=review.sections[j];
        if(alternatives(a,b)||complete(a)||complete(b))continue;
        if(std::min(a.last_station_m,b.last_station_m)>=std::max(a.first_station_m,b.first_station_m)-1e-6||!separated(a.candidate,b.candidate,p.road_width_m))return false;
    }
    if(review.connected&&!review.gaps.empty())return false;
    double previous=-1;
    for(const auto& gap:review.gaps){if(!std::isfinite(gap.first_station_m)||!std::isfinite(gap.last_station_m)||gap.first_station_m<0||gap.last_station_m<gap.first_station_m||gap.first_station_m<previous-1e-6||!finite(gap.start)||!finite(gap.end)||gap.reason.empty())return false;previous=gap.last_station_m;
        for(const auto& section:review.sections)if(std::min(gap.last_station_m,section.last_station_m)>std::max(gap.first_station_m,section.first_station_m)+1e-5)return false;}
    return true;
}

DesignReview review_surface_corridor(const Terrain& terrain,const SearchResult& corridor,const Parameters& p,const UnitCosts& costs,const SearchOptions& resources,const RoadOptions& options,
    std::stop_token stop,ProgressCallback progress,std::function<void(const DesignReview&)> update){
    (void)measure(corridor.route);const auto station=stations(corridor.route);DesignReview review;
    auto publish=[&]{gaps(review,corridor.route,station);std::ostringstream out;out<<"Revisión con parámetros originales: pendiente "<<p.max_grade_percent<<" %, radio "<<p.min_radius_m<<" m, recta al invertir giro "<<p.min_straight_m<<" m y separación entre vueltas "<<p.road_width_m<<" m.\n";
        out<<review.sections.size()<<" alternativas de tramos verificadas; "<<review.gaps.size()<<" conexiones pendientes. El corredor gris no acredita diseño vial.\n";
        if(review.connected)out<<"Conexión A/B verificada; se prioriza la menor longitud horizontal entre las alternativas revisadas.\n";
        if(stop.stop_requested())out<<"Búsqueda cancelada; se conservan los tramos que ya pasaron la validación.\n";
        else out<<"Búsqueda acotada, sin garantía de encontrar todas las soluciones ni el óptimo global.\n";
        review.report=out.str();if(update)update(review);
    };
    if(progress)progress(0);publish();if(stop.stop_requested())return review;
    auto accept=[&](SearchResult candidate,double a,double b){
        if(!checked(terrain,candidate,p))return false;
        for(const auto& old:review.sections)if(std::abs(a-old.first_station_m)<1e-6&&std::abs(b-old.last_station_m)<1e-6&&equal_path(old.candidate,candidate))return false;
        const bool complete=a<1e-6&&b>station.back()-1e-6;
        if(!complete)for(const auto& old:review.sections){
            if(std::abs(a-old.first_station_m)<1e-6&&std::abs(b-old.last_station_m)<1e-6&&same(candidate.route.front(),old.candidate.route.front())&&same(candidate.route.back(),old.candidate.route.back()))continue;
            if(old.first_station_m<1e-6&&old.last_station_m>station.back()-1e-6)continue;
            if(std::min(b,old.last_station_m)>=std::max(a,old.first_station_m)-1e-6||!separated(old.candidate,candidate,p.road_width_m))return false;
        }
        candidate.estimate=estimate_cost(candidate.route,terrain,p,costs,candidate.curves);
        if(a<1e-6&&b>station.back()-1e-6){if(!review.connected||candidate.estimate->validation.metrics.horizontal_m<review.connected->estimate->validation.metrics.horizontal_m-1e-6)review.connected=candidate;}
        if(review.sections.size()<128)review.sections.push_back({std::move(candidate),a,b});publish();return true;
    };
    // Publish useful local pieces first. Windows are longer than the turning
    // scale; microscopic terrain zigzags are never promoted individually.
    const double minimum=std::max({p.min_straight_m,2*p.min_radius_m,4*p.road_width_m,40.});
    const double window=std::max(minimum*2,station.back()/24.);
    std::deque<std::pair<std::size_t,std::size_t>> windows;
    auto index=[&](double s){auto it=std::lower_bound(station.begin(),station.end(),s);return std::min<std::size_t>(station.size()-1,static_cast<std::size_t>(it-station.begin()));};
    auto before=[&](double s){auto it=std::upper_bound(station.begin(),station.end(),s);return it==station.begin()?std::size_t{0}:static_cast<std::size_t>(it-station.begin()-1);};
    const double buffer=std::max({p.road_width_m*2,p.min_straight_m*0.5,5.});
    for(double s=0;s+minimum<=station.back()+1e-6&&windows.size()<48;s+=window+buffer){auto first=index(s),last=before(std::min(station.back(),s+window));if(first<last&&station[last]-station[first]>=minimum)windows.push_back({first,last});}
    if(windows.empty())windows.push_back({0,corridor.route.size()-1});
    else if(windows.back().second!=corridor.route.size()-1){auto first=index(std::max(0.,station.back()-window));if(first<corridor.route.size()-1&&station.back()-station[first]>=minimum)windows.push_back({first,corridor.route.size()-1});}
    std::vector<std::pair<std::size_t,std::size_t>> missing;
    // Failed long windows are subdivided around an explicit gray buffer. This
    // salvages meaningful straight/curved pieces without promoting tiny wiggles.
    constexpr std::size_t window_budget=192;std::size_t visited=0;
    while(!windows.empty()&&visited<window_budget&&!stop.stop_requested()){
        auto [first,last]=windows.front();windows.pop_front();++visited;
        auto raw=std::span(corridor.route).subspan(first,last-first+1);bool found=false;int alternatives=0;
        for(double tolerance:{0.5,2.,5.,10.,20.,40.,80.}){if(stop.stop_requested())break;auto candidate=fit(terrain,raw,p,costs,tolerance,stop);if(candidate&&candidate->estimate->validation.metrics.horizontal_m>=minimum&&accept(std::move(*candidate),station[first],station[last])){found=true;if(++alternatives>=2)break;}}
        if(!found){
            bool subdivided=false;const double middle=std::midpoint(station[first],station[last]);
            if(station[last]-station[first]>=2*minimum+buffer){
                auto left=before(middle-buffer/2),right=index(middle+buffer/2);
                if(left>first&&left<last&&station[left]-station[first]>=minimum){windows.push_back({first,left});subdivided=true;}
                if(right>first&&right<last&&station[last]-station[right]>=minimum){windows.push_back({right,last});subdivided=true;}
            }
            if(!subdivided)missing.push_back({first,last});
        }
        if(progress)progress(5+static_cast<int>(visited*40/window_budget));
    }
    // Attempt complete smoothing before bounded lattice searches. The original
    // constraints, including radius and spacing, are rechecked after fitting.
    if(!stop.stop_requested())for(double tolerance:{0.5,2.,5.,10.,20.,40.,80.,160.}){auto candidate=fit(terrain,corridor.route,p,costs,tolerance,stop);if(candidate)accept(std::move(*candidate),0,station.back());}
    auto limited=resources;limited.max_states=std::min<std::size_t>(resources.max_states,6000);limited.max_expansions=std::min<std::size_t>(resources.max_expansions,1000);limited.max_edges=std::min<std::size_t>(resources.max_edges,18000);
    auto road=options;road.adaptive_resolution=false;road.first_feasible=true;road.propose_earthwork=false;road.max_ramp_candidates=std::min<std::size_t>(options.max_ramp_candidates,12);
    if(road.trace)road.trace=[trace=road.trace](std::span<const Vec3> path,bool){trace(path,false);};
    const auto local_count=std::min<std::size_t>(6,missing.size());
    for(std::size_t n=0;n<local_count&&!stop.stop_requested();++n){auto [first,last]=missing[n*(missing.size()-1)/std::max<std::size_t>(1,local_count-1)];
        auto a=corridor.route[first],b=corridor.route[last];road.start_heading.reset();
        road.finish_heading=heading(corridor.route[last-1],b);
        try{auto candidate=plan_continuous_ramp(terrain,a,b,p,costs,limited,road,stop);if(candidate.status==SearchStatus::found)accept(std::move(candidate),station[first],station[last]);}catch(const std::exception&){}
        if(progress)progress(50+static_cast<int>((n+1)*20/std::max<std::size_t>(1,local_count)));
    }
    // Continue looking for a connected usable alignment after gray/local output.
    // A fixed arrival pose avoids expensive mesh/spiral restarts; the departure
    // remains free to explore all compass and contour headings on steep ground.
    if(!stop.stop_requested()&&p.min_radius_m>0){road.first_feasible=false;
        road.start_heading.reset();road.finish_heading=heading(corridor.route[corridor.route.size()-2],corridor.route.back());
        limited.max_states=std::min<std::size_t>(resources.max_states,15000);limited.max_expansions=std::min<std::size_t>(resources.max_expansions,3000);limited.max_edges=std::min<std::size_t>(resources.max_edges,50000);
        try{auto candidate=plan_continuous_ramp(terrain,corridor.route.front(),corridor.route.back(),p,costs,limited,road,stop,[&](int value){if(progress)progress(75+value*24/100);});if(candidate.status==SearchStatus::found)accept(std::move(candidate),0,station.back());}catch(const std::exception&){}
    }
    publish();if(progress&&!stop.stop_requested())progress(100);return review;
}
}
