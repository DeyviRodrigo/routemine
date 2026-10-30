#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <numbers>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace routemine {
namespace {
constexpr double pi=std::numbers::pi,rad=pi/180;
constexpr auto none=std::numeric_limits<std::size_t>::max();
bool diagnostics_enabled(){
#ifdef _MSC_VER
    char* value=nullptr;std::size_t length=0;_dupenv_s(&value,&length,"ROUTEMINE_RAMP_DIAGNOSTICS");bool result=value!=nullptr;std::free(value);return result;
#else
    return std::getenv("ROUTEMINE_RAMP_DIAGNOSTICS")!=nullptr;
#endif
}
double dist(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);}
double wrap(double a){a=std::fmod(a,2*pi);return a<0?a+2*pi:a;}
Vec3 travel(Vec3 a,double heading,double curvature,double length){
    if(std::abs(curvature)<1e-12)return {a.x+length*std::cos(heading),a.y+length*std::sin(heading),a.z};
    double end=heading+curvature*length;
    return {a.x+(std::sin(end)-std::sin(heading))/curvature,a.y+(std::cos(heading)-std::cos(end))/curvature,a.z};
}
double point_distance(Vec3 p,Vec3 a,Vec3 b){double x=b.x-a.x,y=b.y-a.y,t=std::clamp(((p.x-a.x)*x+(p.y-a.y)*y)/(x*x+y*y),0.0,1.0);return std::hypot(p.x-a.x-t*x,p.y-a.y-t*y);}
double cross(Vec3 a,Vec3 b,Vec3 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
double segment_distance(Vec3 a,Vec3 b,Vec3 c,Vec3 d){
    if(std::min(a.x,b.x)<=std::max(c.x,d.x)&&std::min(c.x,d.x)<=std::max(a.x,b.x)&&std::min(a.y,b.y)<=std::max(c.y,d.y)&&std::min(c.y,d.y)<=std::max(a.y,b.y)&&cross(a,b,c)*cross(a,b,d)<=0&&cross(c,d,a)*cross(c,d,b)<=0)return 0;
    return std::min({point_distance(a,c,d),point_distance(b,c,d),point_distance(c,a,b),point_distance(d,a,b)});
}
struct Key {long long x,y,z;int heading,direction,straight;bool operator==(const Key&)const=default;};
struct Hash {std::size_t operator()(Key k)const{return std::hash<long long>{}(k.x)^(std::hash<long long>{}(k.y)<<1)^(std::hash<long long>{}(k.z)<<2)^(std::hash<int>{}(k.heading)<<3)^(std::hash<int>{}(k.direction)<<4)^(std::hash<int>{}(k.straight)<<5);}};
// A sampled height field guides exploration only. Every accepted route is rebuilt
// on original face boundaries and independently checked on the original TIN.
struct HeightField {
    const Terrain& t;Vec3 origin;double spacing;std::size_t nx,ny;std::vector<double> values,guide;bool prepared{};
    HeightField(const Terrain& terrain,double step):t(terrain){auto b=t.bounds();origin=b[0];spacing=std::max(step,std::sqrt((b[1].x-b[0].x)*(b[1].y-b[0].y)/250000));nx=static_cast<std::size_t>(std::ceil((b[1].x-origin.x)/spacing))+1;ny=static_cast<std::size_t>(std::ceil((b[1].y-origin.y)/spacing))+1;values.assign(nx*ny,std::numeric_limits<double>::quiet_NaN());}
    std::optional<double> at(Vec3 q){
        double x=(q.x-origin.x)/spacing,y=(q.y-origin.y)/spacing;
        if(x<0||y<0||x>=nx-1||y>=ny-1)return t.elevation(q.x,q.y);
        auto ix=static_cast<std::size_t>(x),iy=static_cast<std::size_t>(y);double zs[4];
        for(int k=0;k<4;++k){auto id=ix+static_cast<std::size_t>(k%2)+nx*(iy+static_cast<std::size_t>(k/2));if(std::isnan(values[id])){auto z=t.elevation(origin.x+(ix+k%2)*spacing,origin.y+(iy+k/2)*spacing);values[id]=z?*z:std::numeric_limits<double>::infinity();}zs[k]=values[id];if(!std::isfinite(zs[k]))return t.elevation(q.x,q.y);}
        return std::lerp(std::lerp(zs[0],zs[1],x-ix),std::lerp(zs[2],zs[3],x-ix),y-iy);
    }
    void prepare(Vec3 finish,double grade,std::stop_token stop){
        if(prepared||grade==0)return;prepared=true;
        auto closest=none;double nearest=std::numeric_limits<double>::infinity();
        for(std::size_t i=0;i<values.size();++i){if(stop.stop_requested())return;Vec3 q{origin.x+(i%nx)*spacing,origin.y+(i/nx)*spacing,0};
            if(std::isnan(values[i])){auto z=t.elevation(q.x,q.y);values[i]=z?*z:std::numeric_limits<double>::infinity();}
            if(!std::isfinite(values[i]))continue;double d=dist(q,finish)+std::abs(values[i]-finish.z);if(d<nearest){nearest=d;closest=i;}}
        if(closest==none)return;
        guide.assign(values.size(),std::numeric_limits<double>::infinity());guide[closest]=0;
        using E=std::pair<double,std::size_t>;std::priority_queue<E,std::vector<E>,std::greater<E>> queue;queue.emplace(0,closest);
        while(!queue.empty()){
            if(stop.stop_requested())return;auto [cost,id]=queue.top();queue.pop();if(cost>guide[id]+1e-8)continue;
            auto x=static_cast<long long>(id%nx),y=static_cast<long long>(id/nx);
            for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy)continue;auto xx=x+dx,yy=y+dy;if(xx<0||yy<0||xx>=static_cast<long long>(nx)||yy>=static_cast<long long>(ny))continue;
                auto next=static_cast<std::size_t>(xx)+nx*static_cast<std::size_t>(yy);if(!std::isfinite(values[next]))continue;
                double edge=std::max(spacing*std::hypot(dx,dy),std::abs(values[next]-values[id])/grade),candidate=cost+edge;
                if(candidate<guide[next]){guide[next]=candidate;queue.emplace(candidate,next);}}
        }
    }
    double potential(Vec3 q,double grade){
        auto ix=std::clamp<long long>(std::llround((q.x-origin.x)/spacing),0,static_cast<long long>(nx)-1),iy=std::clamp<long long>(std::llround((q.y-origin.y)/spacing),0,static_cast<long long>(ny)-1);
        auto id=static_cast<std::size_t>(ix)+nx*static_cast<std::size_t>(iy);if(guide.empty()||!std::isfinite(guide[id]))return 0;
        return std::max(0.0,guide[id]-std::abs(q.z-values[id])/grade);
    }
};

// Recover a useful corridor from the unconstrained terrain planner, but never
// return its sharp polyline as a road. Fit actual tangent circular fillets and
// independently check the complete fitted geometry against the requested
// radius, grade, separation and excavation envelope.
static SearchResult rounded_surface_seed(const Terrain& terrain,Vec3 start,Vec3 finish,const Parameters& p,
    const UnitCosts& costs,const SearchOptions& resources,const RoadOptions& road,std::stop_token stop){
    SearchResult best;best.strategy="continuous_ramp";best.effective_xy_spacing_m=p.sample_spacing_m;best.effective_heading_step_degrees=1;
    if(road.start_heading||road.finish_heading||stop.stop_requested())return best;
    auto relaxed=p;relaxed.continuous_curves=false;relaxed.min_radius_m=0;relaxed.min_straight_m=0;relaxed.min_straight_same_direction_m=0;relaxed.apply_dg_grade_guidance=false;relaxed.max_turn_degrees=180;relaxed.mode=DesignMode::surface;
    auto options=road;options.propose_earthwork=false;
    // The relaxed surface corridor is exploratory until its curves are fitted.
    // Forward its whole search as gray history, never as a validated blue route.
    if(road.trace)options.trace=[trace=road.trace](std::span<const Vec3> path,bool){trace(path,false);};
    auto corridor=plan_road(terrain,start,finish,relaxed,costs,resources,options,stop).surface;
    best.expanded=corridor.expanded;best.edges_checked=corridor.edges_checked;
    if(corridor.status!=SearchStatus::found){best.status=corridor.status;return best;}
    const auto& raw=corridor.route;
    for(double tolerance:{0.5,1.0,2.0,4.0,8.0,16.0,32.0,64.0,96.0,128.0}){
        if(stop.stop_requested()){best.status=SearchStatus::cancelled;return best;}
        std::vector<std::size_t> keep{0,raw.size()-1};std::vector<std::pair<std::size_t,std::size_t>> ranges{{0,raw.size()-1}};
        while(!ranges.empty()){
            auto [a,b]=ranges.back();ranges.pop_back();double maximum=tolerance;auto selected=none;
            for(auto i=a+1;i<b;++i){double d=point_distance(raw[i],raw[a],raw[b]);if(d>maximum){maximum=d;selected=i;}}
            if(selected!=none){keep.push_back(selected);ranges.emplace_back(a,selected);ranges.emplace_back(selected,b);}
        }
        std::sort(keep.begin(),keep.end());std::vector<Vec3> path;for(auto i:keep)path.push_back(raw[i]);
        struct Corner {double turn{},offset{};Vec3 begin,end,center;};std::vector<Corner> corners(path.size());bool fits=true;
        for(std::size_t i=1;i+1<path.size();++i){
            double incoming=std::atan2(path[i].y-path[i-1].y,path[i].x-path[i-1].x),outgoing=std::atan2(path[i+1].y-path[i].y,path[i+1].x-path[i].x);
            auto& c=corners[i];c.turn=std::remainder(outgoing-incoming,2*pi);
            if(std::abs(c.turn)<1e-7)continue;
            c.offset=p.min_radius_m*std::tan(std::abs(c.turn)/2);
            if(!std::isfinite(c.offset)){fits=false;break;}
            c.begin={path[i].x-c.offset*std::cos(incoming),path[i].y-c.offset*std::sin(incoming),0};
            c.end={path[i].x+c.offset*std::cos(outgoing),path[i].y+c.offset*std::sin(outgoing),0};
            double sign=std::copysign(1.0,c.turn);c.center={c.begin.x-sign*p.min_radius_m*std::sin(incoming),c.begin.y+sign*p.min_radius_m*std::cos(incoming),0};
        }
        for(std::size_t i=1;i<path.size();++i)if(corners[i-1].offset+corners[i].offset>dist(path[i-1],path[i])-1e-6)fits=false;
        if(!fits){if(diagnostics_enabled())std::cerr<<"fillet tolerance="<<tolerance<<" vertices="<<path.size()<<" rejected=overlapping_tangents\n";continue;}
        std::vector<Vec3> route{start};std::vector<AlignmentCurve> curves;
        auto append=[&](Vec3 q){
            if(stop.stop_requested())return false;
            if(dist(route.back(),q)<=1e-7)return true;
            auto profile=terrain.segment_profile(route.back(),q);if(!profile)return false;
            for(auto v:*profile)if(dist(route.back(),v)>1e-7){if(route.size()>=p.max_samples)return false;route.push_back(v);}
            return true;
        };
        for(std::size_t i=1;i+1<path.size()&&fits;++i){auto& c=corners[i];
            if(std::abs(c.turn)<1e-7){fits=append(path[i]);continue;}
            fits=append(c.begin);if(!fits)break;auto first=route.size()-1;
            auto count=static_cast<std::size_t>(std::ceil(std::max(std::abs(c.turn)/rad,p.min_radius_m*std::abs(c.turn)/p.sample_spacing_m)));
            double a=std::atan2(c.begin.y-c.center.y,c.begin.x-c.center.x);
            for(std::size_t j=1;j<=count&&fits;++j){double angle=a+c.turn*j/count;fits=append(j==count?c.end:Vec3{c.center.x+p.min_radius_m*std::cos(angle),c.center.y+p.min_radius_m*std::sin(angle),0});}
            if(fits)curves.push_back({first,route.size()-1,c.center,c.turn/rad});
        }
        if(!fits||!append(finish))continue;route.front()=start;route.back()=finish;
        if(p.mode==DesignMode::earthwork){
            std::vector<double> lo(route.size()),hi(route.size());double grade=p.max_grade_percent/100*0.999;
            for(std::size_t i=0;i<route.size();++i){lo[i]=route[i].z-p.max_cut_m;hi[i]=route[i].z+p.max_fill_m;}
            lo.front()=hi.front()=start.z;lo.back()=hi.back()=finish.z;
            for(std::size_t i=1;i<route.size();++i){double rise=grade*dist(route[i-1],route[i]);lo[i]=std::max(lo[i],lo[i-1]-rise);hi[i]=std::min(hi[i],hi[i-1]+rise);if(lo[i]>hi[i]+1e-8){fits=false;break;}}
            for(std::size_t i=route.size()-1;i>0&&fits;--i){double rise=grade*dist(route[i-1],route[i]);lo[i-1]=std::max(lo[i-1],lo[i]-rise);hi[i-1]=std::min(hi[i-1],hi[i]+rise);if(lo[i-1]>hi[i-1]+1e-8)fits=false;}
            for(std::size_t i=1;i<route.size()&&fits;++i){double rise=grade*dist(route[i-1],route[i]),lower=std::max(lo[i],route[i-1].z-rise),upper=std::min(hi[i],route[i-1].z+rise);if(lower>upper+1e-8)fits=false;else route[i].z=std::clamp(route[i].z,lower,std::max(lower,upper));}
        }
        if(!fits)continue;auto validation=validate(route,terrain,p,curves);if(!validation.checked_constraints_pass()){
            if(diagnostics_enabled()){std::cerr<<"fillet tolerance="<<tolerance<<" grade="<<validation.metrics.max_grade_percent<<" rejected=";for(auto& issue:validation.violations)std::cerr<<issue<<',';std::cerr<<'\n';}continue;}
        auto estimate=estimate_cost(route,terrain,p,costs,curves);
        if(best.status!=SearchStatus::found||estimate.total<best.estimate->total){best.status=SearchStatus::found;best.route=std::move(route);best.curves=std::move(curves);best.estimate=std::move(estimate);}
    }
    if(best.status==SearchStatus::found&&road.trace)road.trace(best.route,true);
    if(best.status!=SearchStatus::found)best.reference_route=corridor.route;
    return best;
}

// Explicit winding candidates complement the local lattice. A logarithmic
// spiral changes radius gradually around a hill/pit rather than stacking circles
// at the same XY position. Each small interval is represented by two tangent
// circular arcs (a biarc); these are actual exported and validated geometry.
static SearchResult spiral_seed(const Terrain& terrain,Vec3 start,Vec3 finish,const Parameters& p,
    const UnitCosts& costs,const RoadOptions& road,std::stop_token stop){
    SearchResult best;best.strategy="continuous_ramp";best.effective_xy_spacing_m=p.sample_spacing_m;best.effective_heading_step_degrees=1;
    if(road.start_heading||road.finish_heading)return best;
    auto bounds=terrain.bounds();std::vector<Vec3> centers{{(bounds[0].x+bounds[1].x)/2,(bounds[0].y+bounds[1].y)/2,0}};
    for(bool high:{false,true}){Vec3 center{};double weight=0;
        for(auto v:terrain.vertices()){
            double w=(high?v.z-bounds[0].z:bounds[1].z-v.z)/std::max(1.0,bounds[1].z-bounds[0].z);
            w=std::pow(std::max(0.0,w),4);center.x+=v.x*w;center.y+=v.y*w;weight+=w;
        }
        if(weight>0){center.x/=weight;center.y/=weight;centers.push_back(center);}
    }
    for(auto center:centers){double ra=dist(start,center),rb=dist(finish,center);
        if(!finite(center)||!std::isfinite(ra)||!std::isfinite(rb))continue;
        if(std::min(ra,rb)<p.min_radius_m*1.02)continue;
        double a=std::atan2(start.y-center.y,start.x-center.x),b=std::atan2(finish.y-center.y,finish.x-center.x);
        for(double sign:{-1.0,1.0})for(int turns=0;turns<=8;++turns){
            if(stop.stop_requested()){best.status=SearchStatus::cancelled;return best;}
            double sweep=sign*(wrap(sign*(b-a))+2*pi*turns);if(std::abs(sweep)<0.1)continue;
            double logarithm=std::log(rb/ra),k=logarithm/sweep;
            // Arc curvature of a logarithmic spiral is 1/(r*sqrt(1+k²)).
            if(std::min(ra,rb)*std::sqrt(1+k*k)<p.min_radius_m*1.001)continue;
            double approx_length=std::abs(logarithm)<1e-8?ra*std::abs(sweep):std::abs((rb-ra)/k)*std::sqrt(1+k*k);
            if(std::abs(start.z-finish.z)>p.max_grade_percent/100*approx_length+1e-7)continue;
            std::vector<Vec3> route{start};std::vector<AlignmentCurve> curves;bool valid=true;
            auto append=[&](Vec3 q){if(stop.stop_requested())return false;if(dist(route.back(),q)<=1e-7)return true;auto profile=terrain.segment_profile(route.back(),q);if(!profile)return false;
                for(auto v:*profile)if(dist(route.back(),v)>1e-7){if(route.size()>=p.max_samples)return false;route.push_back(v);}return true;};
            auto arc=[&](Vec3 begin,double heading,Vec3 end){
                double dx=end.x-begin.x,dy=end.y-begin.y,l2=dx*dx+dy*dy,den=2*(std::cos(heading)*dy-std::sin(heading)*dx);
                if(std::abs(den)<1e-10)return append(end);
                double radius=l2/den;if(std::abs(radius)<p.min_radius_m-1e-6)return false;
                Vec3 c{begin.x-radius*std::sin(heading),begin.y+radius*std::cos(heading),0};
                double from=std::atan2(begin.y-c.y,begin.x-c.x),to=std::atan2(end.y-c.y,end.x-c.x),turn=std::copysign(wrap(std::copysign(1.0,radius)*(to-from)),radius);
                if(std::abs(turn)>pi)return false;auto first=route.size()-1;
                double count_d=std::ceil(std::max(std::abs(turn)/rad,std::abs(radius*turn)/p.sample_spacing_m));
                if(!std::isfinite(count_d)||count_d<1||count_d>static_cast<double>(p.max_samples))return false;
                auto count=static_cast<std::size_t>(count_d);
                for(std::size_t j=1;j<=count;++j){double angle=from+turn*j/count;if(!append(j==count?end:Vec3{c.x+std::abs(radius)*std::cos(angle),c.y+std::abs(radius)*std::sin(angle),0}))return false;}
                curves.push_back({first,route.size()-1,c,turn/rad});return true;
            };
            auto count=static_cast<std::size_t>(std::ceil(std::abs(sweep)/(5*rad)));Vec3 previous=start;
            auto tangent=[&](double angle){return angle+std::atan2(sign,sign*k);};
            double previous_heading=tangent(a);
            for(std::size_t i=1;i<=count&&valid;++i){
                double t=static_cast<double>(i)/count,angle=a+sweep*t,r=ra*std::exp(logarithm*t);
                Vec3 q=i==count?finish:Vec3{center.x+r*std::cos(angle),center.y+r*std::sin(angle),0};double next_heading=tangent(angle);
                double ux=std::cos(previous_heading),uy=std::sin(previous_heading),vx=std::cos(next_heading),vy=std::sin(next_heading);
                double dx=q.x-previous.x,dy=q.y-previous.y,A=2*(1-ux*vx-uy*vy),B=dx*(ux+vx)+dy*(uy+vy),C=dx*dx+dy*dy;
                double d=std::abs(A)<1e-10?C/(2*B):(-B+std::sqrt(B*B+A*C))/A;
                Vec3 middle{(previous.x+q.x+d*(ux-vx))/2,(previous.y+q.y+d*(uy-vy))/2,0};
                valid=arc(previous,previous_heading,middle);
                if(valid){
                    // The second circle is constructed from the endpoint with
                    // reversed tangent, then sampled in the forward direction.
                    double ex=middle.x-q.x,ey=middle.y-q.y,den=2*(-vx*ey+vy*ex),radius=(ex*ex+ey*ey)/den;
                    if(!std::isfinite(radius)||std::abs(radius)<p.min_radius_m-1e-6){valid=false;break;}
                    Vec3 c{q.x+radius*vy,q.y-radius*vx,0};double h=std::atan2(middle.y-c.y,middle.x-c.x)-std::copysign(pi/2,radius);
                    valid=arc(middle,h,q);
                }
                previous=q;previous_heading=next_heading;
            }
            if(!valid)continue;route.front()=start;route.back()=finish;
            if(p.mode==DesignMode::earthwork){
                std::vector<double> lo(route.size()),hi(route.size());double grade=p.max_grade_percent/100*0.999;
                for(std::size_t i=0;i<route.size();++i){lo[i]=route[i].z-p.max_cut_m;hi[i]=route[i].z+p.max_fill_m;}
                lo.front()=hi.front()=start.z;lo.back()=hi.back()=finish.z;
                for(std::size_t i=1;i<route.size();++i){double rise=grade*dist(route[i-1],route[i]);lo[i]=std::max(lo[i],lo[i-1]-rise);hi[i]=std::min(hi[i],hi[i-1]+rise);if(lo[i]>hi[i]+1e-8){valid=false;break;}}
                for(std::size_t i=route.size()-1;i>0&&valid;--i){double rise=grade*dist(route[i-1],route[i]);lo[i-1]=std::max(lo[i-1],lo[i]-rise);hi[i-1]=std::min(hi[i-1],hi[i]+rise);if(lo[i-1]>hi[i-1]+1e-8)valid=false;}
                for(std::size_t i=1;i<route.size()&&valid;++i){double rise=grade*dist(route[i-1],route[i]),lower=std::max(lo[i],route[i-1].z-rise),upper=std::min(hi[i],route[i-1].z+rise);if(lower>upper+1e-8)valid=false;else route[i].z=std::clamp(route[i].z,lower,std::max(lower,upper));}
            }
            if(!valid)continue;auto v=validate(route,terrain,p,curves);if(!v.checked_constraints_pass()){
                if(diagnostics_enabled()){std::cerr<<"spiral center="<<center.x<<','<<center.y<<" sign="<<sign<<" turns="<<turns<<" length="<<v.metrics.horizontal_m<<" grade="<<v.metrics.max_grade_percent<<" rejected=";for(auto& issue:v.violations)std::cerr<<issue<<',';std::cerr<<'\n';}continue;}
            auto estimate=estimate_cost(route,terrain,p,costs,curves);
            if(best.status!=SearchStatus::found||estimate.total<best.estimate->total){best.status=SearchStatus::found;best.route=std::move(route);best.curves=std::move(curves);best.estimate=std::move(estimate);}
            if(road.first_feasible){if(road.trace)road.trace(best.route,true);return best;}
        }
    }
    if(best.status==SearchStatus::found&&road.trace)road.trace(best.route,true);return best;
}
}

static SearchResult continuous_attempt(const Terrain& terrain,Vec3 start,Vec3 finish,const Parameters& p,const UnitCosts& costs,
                                  const SearchOptions& resources,const RoadOptions& road,HeightField& field,double margin,bool bottom_up,double terrain_weight,std::stop_token stop,const ProgressCallback& progress,std::span<const Vec3> corridor={}) {
    SearchResult result;result.strategy="continuous_ramp";
    if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
    bool reverse=start.z<finish.z;if(reverse)std::swap(start,finish);
    if(bottom_up){std::swap(start,finish);reverse=!reverse;}
    const bool diagnostics=diagnostics_enabled();
    if(diagnostics)std::cerr<<"ramp_attempt mode="<<(p.mode==DesignMode::surface?"surface":"earthwork")<<" bottom_up="<<bottom_up<<" guide="<<corridor.size()<<" margin="<<margin<<" terrain_weight="<<terrain_weight<<'\n';
    auto required_start=reverse?road.finish_heading:road.start_heading;
    auto required_finish=reverse?road.start_heading:road.finish_heading;
    if(reverse){if(required_start)*required_start=wrap(*required_start+pi);if(required_finish)*required_finish=wrap(*required_finish+pi);}
    double lowest=std::min(start.z,finish.z);
    double R=p.min_radius_m,step=std::clamp(resources.xy_spacing_m*2,8.0,25.0),g=p.max_grade_percent/100;
    double cell=std::min(resources.xy_spacing_m,p.mode==DesignMode::surface?2.0:step/2),zcell=std::max(0.25,g*step/2),angle_step=std::min(p.mode==DesignMode::surface?(terrain_weight>0?0.25:2.0):7.5,road.heading_step_degrees)*rad;
    double cut=p.mode==DesignMode::earthwork?p.max_cut_m:p.surface_tolerance_m,fill=p.mode==DesignMode::earthwork?p.max_fill_m:p.surface_tolerance_m;
    double search_cut=cut-std::min(margin,cut*0.2),search_fill=fill-std::min(margin,fill*0.2);
    // Additive preliminary cost, with no material reuse (the search contract).
    // Normalize currency without changing the relative length/cut/fill weights.
    double road_cost=p.road_width_m*costs.road_per_m2;
    double cut_cost=p.road_width_m*((1-costs.rock_fraction)*costs.soil_cut_per_m3+costs.rock_fraction*costs.rock_cut_per_m3+costs.disposal_per_m3+costs.haul_per_m3_km*costs.disposal_haul_km);
    double fill_cost=p.road_width_m*(costs.fill_placement_per_m3+costs.imported_material_per_m3+costs.haul_per_m3_km*costs.import_haul_km);
    double scale=std::max({road_cost,cut_cost,fill_cost});
    if(scale==0){road_cost=1;scale=1;}
    road_cost/=scale;cut_cost/=scale;fill_cost/=scale;
    auto edge_cost=[&](double length,double depth){return length*(road_cost+std::max(0.0,depth)*cut_cost+std::max(0.0,-depth)*fill_cost);};
    auto bounds=terrain.bounds();
    field.prepare(finish,g,stop);
    result.effective_xy_spacing_m=cell;result.effective_heading_step_degrees=angle_step/rad;
    struct Node {Vec3 q;double heading{},cost{},curvature{},length{};std::size_t previous{none};
        double station{};Vec3 block_lo,block_hi;std::size_t jump{none};int direction{};double straight_run{};};
    std::vector<Node> nodes;nodes.reserve(std::min<std::size_t>(resources.max_states,200000));
    std::unordered_map<Key,double,Hash> best;
    const double straight_cap=std::max(p.min_straight_m,p.min_straight_same_direction_m);
    auto key=[&](const Node& n){auto q=n.q;
        const int band=static_cast<int>(std::min(1000.0,std::floor(std::min(n.straight_run,straight_cap)/step)));
        // Keep a state that has reached S/O distinct from a cheaper state
        // just below either threshold, including fractional-metre limits.
        const int qualified=(n.straight_run+1e-6>=p.min_straight_m?1:0)+(n.straight_run+1e-6>=straight_cap?2:0);
        return Key{std::llround((q.x-bounds[0].x)/cell),std::llround((q.y-bounds[0].y)/cell),std::llround((q.z-bounds[0].z)/zcell),static_cast<int>(std::llround(wrap(n.heading)/angle_step))%static_cast<int>(std::llround(2*pi/angle_step)),n.direction,4*band+qualified};};
    auto turn_allowed=[&](const Node& n,double curvature){
        if(std::abs(curvature)<=1e-12)return true;
        const bool positive_tangent=n.straight_run>1e-6,reversal=n.direction*curvature<0;
        if(!positive_tangent&&!reversal)return true;
        const double required=positive_tangent&&n.direction*curvature>0?straight_cap:p.min_straight_m;
        return n.straight_run+1e-6>=required;
    };
    std::vector<Vec3> guide;std::vector<double> remaining;
    if(corridor.size()>1){
        std::vector<Vec3> ordered(corridor.begin(),corridor.end());if(dist(ordered.front(),start)>dist(ordered.back(),start))std::reverse(ordered.begin(),ordered.end());
        guide.push_back(ordered.front());double station=0;
        for(std::size_t i=1;i<ordered.size();++i){station+=dist(ordered[i-1],ordered[i]);if(station>=std::max(10.0,R)){guide.push_back(ordered[i]);station=0;}}
        if(dist(guide.back(),finish)>1e-7)guide.push_back(finish);remaining.resize(guide.size());
        for(std::size_t i=guide.size()-1;i>0;--i)remaining[i-1]=remaining[i]+dist(guide[i-1],guide[i]);
    }
    auto nearest_guide=[&](Vec3 q){std::size_t nearest=0;double best_distance=std::numeric_limits<double>::infinity();
        for(std::size_t i=0;i<guide.size();++i){double dx=q.x-guide[i].x,dy=q.y-guide[i].y,dz=(q.z-guide[i].z)/std::max(0.01,g),d=dx*dx+dy*dy+dz*dz;
            if(d<best_distance){best_distance=d;nearest=i;}}
        return nearest;
    };
    auto heuristic=[&](Vec3 q){
        if(!guide.empty()){auto i=nearest_guide(q);return remaining[i]+dist(q,guide[i])+std::abs(q.z-guide[i].z)/std::max(0.01,g);}
        return std::max({dist(q,finish),(g>0?std::abs(q.z-finish.z)/g:0),field.potential(q,g)});
    };
    using Entry=std::pair<double,std::size_t>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    bool budget=false;
    auto initialize=[&](Node& n){
        if(n.previous!=none){const auto& prev=nodes[n.previous];n.direction=prev.direction;n.straight_run=prev.straight_run;
            if(std::abs(n.curvature)>1e-12){n.direction=n.curvature>0?1:-1;n.straight_run=0;}else n.straight_run+=n.length;}
        n.station=n.previous==none?0:nodes[n.previous].station+n.length;n.block_lo=n.block_hi=n.q;
        auto cursor=n.previous;for(int i=0;i<16&&cursor!=none;++i){auto q=nodes[cursor].q;n.block_lo.x=std::min(n.block_lo.x,q.x);n.block_lo.y=std::min(n.block_lo.y,q.y);n.block_hi.x=std::max(n.block_hi.x,q.x);n.block_hi.y=std::max(n.block_hi.y,q.y);n.jump=cursor;cursor=nodes[cursor].previous;}};
    auto push=[&](Node n){initialize(n);auto k=key(n);auto it=best.find(k);if(it!=best.end()&&it->second<=n.cost+1e-8)return;
        if(nodes.size()>=resources.max_states){budget=true;return;}best[k]=n.cost;auto id=nodes.size();nodes.push_back(n);queue.emplace(n.cost+4*road_cost*heuristic(n.q),id);};
    // Prune definite self-crossings during exploration, rather than waiting for
    // final reconstruction. Block bounds skip distant ancestry in groups of 16.
    // The sagitta allowance makes the chord test conservative: final validation
    // still checks the sampled arcs and the full requested separation.
    double sagitta=step*step/(8*R),conflict=std::max(0.0,p.road_width_m-2*sagitta),padding=p.road_width_m+2*sagitta;
    auto clear=[&](Vec3 a,Vec3 b,std::size_t parent){
        if(conflict<=0)return true;auto cursor=parent;double station=nodes[parent].station;
        while(cursor!=none&&station-nodes[cursor].station<std::max(3*p.road_width_m,4*step))cursor=nodes[cursor].previous;
        while(cursor!=none&&nodes[cursor].previous!=none){const auto& n=nodes[cursor];
            if(n.block_hi.x<std::min(a.x,b.x)-padding||n.block_lo.x>std::max(a.x,b.x)+padding||n.block_hi.y<std::min(a.y,b.y)-padding||n.block_lo.y>std::max(a.y,b.y)+padding){cursor=n.jump;continue;}
            if(segment_distance(a,b,n.q,nodes[n.previous].q)<conflict){++result.diagnostics.spacing;return false;}cursor=n.previous;}
        return true;
    };
    auto ground=[&](Vec3 q)->std::optional<double>{if(q.x<bounds[0].x||q.x>bounds[1].x||q.y<bounds[0].y||q.y>bounds[1].y)return {};return terrain.elevation(q.x,q.y);};
    auto interval=[&](const Node& n,double curvature,double length)->std::optional<std::array<double,3>> {
        if(++result.edges_checked>resources.max_edges){budget=true;return {};}
        if(p.mode==DesignMode::surface){
            // Follow the terrain through each primitive, rather than require a
            // straight vertical profile within the surface tolerance.
            auto previous=n.q;
            for(int i=1;i<=4;++i){auto q=travel(n.q,n.heading,curvature,length*i/4);auto z=ground(q);if(!z){++result.diagnostics.coverage;return {};}q.z=*z;
                if(std::abs(q.z-previous.z)>g*dist(previous,q)+1e-8){++result.diagnostics.grade;return {};}previous=q;}
            return std::array<double,3>{previous.z,previous.z,0};
        }
        double lo=std::max(lowest,n.q.z-g*length*0.999),hi=n.q.z+g*length*0.999,mean=0;
        for(int i=1;i<=4;++i){double t=i/4.0;auto q=travel(n.q,n.heading,curvature,length*t);auto z=ground(q);if(!z){++result.diagnostics.coverage;return {};}
            lo=std::max(lo,n.q.z+(*z-search_cut-n.q.z)/t);hi=std::min(hi,n.q.z+(*z+search_fill-n.q.z)/t);mean+=std::abs(*z-n.q.z);if(lo>hi+1e-8){++result.diagnostics.earthwork;return {};}}
        return std::array<double,3>{lo,std::max(lo,hi),mean/4};
    };
    // A fixed heading lattice can miss the narrow feasible band on a steep
    // hillside. Seed the exact local contour/grade directions as well as the
    // regular headings. They are continuous poses, not rounded geometry.
    auto slope_headings=[&](Vec3 q){
        std::vector<double> headings;
        if(auto gradient=terrain.surface_gradient(q.x,q.y,false)){
            double slope=std::hypot((*gradient)[0],(*gradient)[1]);
            if(slope>1e-9){double uphill=std::atan2((*gradient)[1],(*gradient)[0]);
                for(double fraction:{-0.95,-0.65,0.0,0.65,0.95}){
                    double offset=std::acos(std::clamp(fraction*g/slope,-1.0,1.0));
                    headings.push_back(wrap(uphill+offset));headings.push_back(wrap(uphill-offset));
                }
            }
        }
        return headings;
    };
    if(required_start)push({start,*required_start});
    else {for(double a=0;a<2*pi-1e-8;a+=std::min(15.0,road.heading_step_degrees)*rad)push({start,a});
        for(auto a:slope_headings(start))push({start,a});
        // A point on a horizontal bench has no local contour direction. Look
        // ahead to nearby faces so a narrow departure band at the bench edge is
        // represented exactly instead of depending on a 7.5 degree seed grid.
        if(p.mode==DesignMode::surface)for(double distance:{R,2*R,4*R})for(int direction=0;direction<16;++direction){
            auto q=travel(start,direction*pi/8,0,distance);for(auto a:slope_headings(q))push({start,a});
        }
    }
    std::size_t goal=none;
    std::size_t rebuilds=0;
    auto failed=[&](const char* reason){
        const std::string code=reason;if(code=="coverage")++result.diagnostics.coverage;else if(code.find("envelope")!=std::string::npos)++result.diagnostics.earthwork;else if(code!="validation")++result.diagnostics.geometry;
        if(diagnostics)std::cerr<<"ramp_candidate="<<rebuilds<<" margin="<<margin<<" rejected="<<reason<<'\n';return false;};
    // Rebuild one candidate from continuous primitives, adding exact face crossings,
    // then solve a bounded Lipschitz vertical profile inside cut/fill envelopes.
    auto rebuild=[&](std::size_t last)->bool {
        ++rebuilds;
        std::vector<std::size_t> chain;for(auto id=last;id!=none;id=nodes[id].previous)chain.push_back(id);std::reverse(chain.begin(),chain.end());
        std::vector<Vec3> route{start};std::vector<double> heights{*terrain.elevation(start.x,start.y)};std::vector<AlignmentCurve> curves;
        for(std::size_t n=1;n<chain.size();++n){if(stop.stop_requested())return false;const auto& prev=nodes[chain[n-1]];const auto& node=nodes[chain[n]];auto first=route.size()-1;
            double turn=node.curvature*node.length;double count_d=std::ceil(std::max(node.length/p.sample_spacing_m,std::abs(turn)/rad));
            if(!std::isfinite(count_d)||count_d>static_cast<double>(p.max_samples))throw std::length_error("Sampling budget exceeded; choose an explicit coarser spacing");
            auto count=static_cast<std::size_t>(std::max(1.0,count_d));
            for(std::size_t i=1;i<=count;++i){double fraction=static_cast<double>(i)/count;auto q=travel(prev.q,prev.heading,node.curvature,node.length*fraction);q.z=std::lerp(prev.q.z,node.q.z,fraction);
                if(i==count)q=node.q;auto a=route.back();auto profile=terrain.segment_profile(a,q);if(!profile)return failed("coverage");
                for(auto v:*profile){double length=dist(a,q),d=dist(a,v);if(d<=1e-7||dist(route.back(),v)<=1e-7)continue;
                    if(route.size()>=p.max_samples)throw std::length_error("Face-boundary sampling budget exceeded");
                    heights.push_back(v.z);v.z=std::lerp(a.z,q.z,std::clamp(d/length,0.0,1.0));route.push_back(v);}
            }
            if(std::abs(turn)>1e-8){Vec3 c{prev.q.x-std::sin(prev.heading)/node.curvature,prev.q.y+std::cos(prev.heading)/node.curvature,0};curves.push_back({first,route.size()-1,c,turn/rad});}
        }
        if(route.size()<2||dist(route.back(),finish)>1e-5)return failed("endpoints");route.back()=finish;
        if(p.mode==DesignMode::surface){
            for(std::size_t i=0;i<route.size();++i)route[i].z=heights[i];
            route.front()=start;route.back()=finish;
        }else{
        std::vector<double> lo(route.size()),hi(route.size());double design_grade=g*0.999;
        for(std::size_t i=0;i<route.size();++i){lo[i]=heights[i]-cut;hi[i]=heights[i]+fill;}
        lo.front()=hi.front()=start.z;lo.back()=hi.back()=finish.z;
        for(std::size_t i=1;i<route.size();++i){double rise=design_grade*dist(route[i-1],route[i]);lo[i]=std::max(lo[i],lo[i-1]-rise);hi[i]=std::min(hi[i],hi[i-1]+rise);if(lo[i]>hi[i]+1e-8)return failed("forward_z_envelope");}
        for(std::size_t i=route.size()-1;i>0;--i){double rise=design_grade*dist(route[i-1],route[i]);lo[i-1]=std::max(lo[i-1],lo[i]-rise);hi[i-1]=std::min(hi[i-1],hi[i]+rise);if(lo[i-1]>hi[i-1]+1e-8)return false;}
        for(std::size_t i=1;i<route.size();++i){double lower=std::max(lo[i],route[i-1].z-design_grade*dist(route[i-1],route[i])),upper=std::min(hi[i],route[i-1].z+design_grade*dist(route[i-1],route[i]));if(lower>upper+1e-8)return false;route[i].z=std::clamp(route[i].z,lower,std::max(lower,upper));}
        }
        // Snapping the final connector must not leave a zero-length segment.
        // Reject this candidate and keep searching instead of aborting the run.
        for(std::size_t i=1;i<route.size();++i)
            if(!finite(route[i])||dist(route[i-1],route[i])<=1e-8)return failed("degenerate_segment");
        auto effective=p;effective.continuous_curves=true;auto v=validate(route,terrain,effective,curves);if(!v.checked_constraints_pass()){
            for(const auto& violation:v.violations){
                if(violation=="grade_limit")++result.diagnostics.grade;
                else if(violation=="cut_limit"||violation=="fill_limit"||violation=="surface_separation")++result.diagnostics.earthwork;
                else if(violation=="terrain_missing_or_ambiguous")++result.diagnostics.coverage;
                else if(violation=="ramp_spacing_or_intersection")++result.diagnostics.spacing;
                else ++result.diagnostics.geometry;
            }
            if(diagnostics)for(const auto& violation:v.violations)std::cerr<<"ramp_violation="<<violation<<'\n';return failed("validation");}
        result.route=std::move(route);result.curves=std::move(curves);
        if(reverse){std::reverse(result.route.begin(),result.route.end());std::reverse(result.curves.begin(),result.curves.end());for(auto& c:result.curves){auto first=c.first;c.first=result.route.size()-1-c.last;c.last=result.route.size()-1-first;c.sweep_degrees=-c.sweep_degrees;}}
        result.estimate=estimate_cost(result.route,terrain,effective,costs,result.curves);if(road.trace)road.trace(result.route,true);return true;
    };
    while(!queue.empty()&&!budget){
        if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
        auto [priority,id]=queue.top();queue.pop();(void)priority;auto n=nodes[id];if(n.cost>best.at(key(n))+1e-8)continue;
        if(road.trace&&n.previous!=none){
            const auto& prev=nodes[n.previous];std::array<Vec3,9> branch;
            for(std::size_t i=0;i<branch.size();++i){double t=static_cast<double>(i)/(branch.size()-1);branch[i]=travel(prev.q,prev.heading,n.curvature,n.length*t);branch[i].z=std::lerp(prev.q.z,n.q.z,t);}
            road.trace(branch,false);
        }
        if(result.expanded++>=resources.max_expansions){budget=true;break;}
        if(diagnostics&&result.expanded%10000==0)std::cerr<<"ramp_expanded="<<result.expanded<<" nodes="<<nodes.size()<<" remaining="<<dist(n.q,finish)<<" elevation="<<n.q.z<<'\n';
        if(progress&&result.expanded%128==0)progress(std::min(99,static_cast<int>(100.0*result.expanded/resources.max_expansions)));
        if(dist(n.q,finish)<std::max(200.0,3*R)){
            using Primitive=std::pair<double,double>;std::vector<std::vector<Primitive>> connections;
            if(required_finish){
                // Circle--straight--circle tangents connect exact endpoint poses.
                // Enumerate all four turn pairs; search handles longer approaches.
                for(double sa:{-1.0,1.0})for(double sb:{-1.0,1.0}){
                    Vec3 ca{n.q.x-sa*R*std::sin(n.heading),n.q.y+sa*R*std::cos(n.heading),0};
                    Vec3 cb{finish.x-sb*R*std::sin(*required_finish),finish.y+sb*R*std::cos(*required_finish),0};
                    double d=dist(ca,cb),offset=(sb-sa)*R;
                    if(d<=1e-7||std::abs(offset)>d)continue;
                    double h=std::atan2(cb.y-ca.y,cb.x-ca.x)-std::asin(std::clamp(offset/d,-1.0,1.0));
                    double line=std::sqrt(std::max(0.0,d*d-offset*offset));
                    double first=wrap(sa*(h-n.heading))*R,last=wrap(sb*(*required_finish-h))*R;
                    connections.push_back({{sa/R,first},{0,line},{sb/R,last}});
                }
            }else for(double k:{0.0,-1/R,1/R}){
                double arc=0,line=0;
                if(k==0){double bearing=std::atan2(finish.y-n.q.y,finish.x-n.q.x);if(std::abs(std::remainder(bearing-n.heading,2*pi))>1e-6)continue;line=dist(n.q,finish);}
                else {auto center=Vec3{n.q.x-std::sin(n.heading)/k,n.q.y+std::cos(n.heading)/k,0};double d=dist(center,finish);if(d<=R+1e-6)continue;
                    double sign=std::copysign(1.0,k),phi=std::atan2(finish.y-center.y,finish.x-center.x),a=phi-sign*std::acos(R/d);double heading=a+sign*pi/2;
                    arc=wrap(sign*(heading-n.heading))/std::abs(k);auto tangent=travel(n.q,n.heading,k,arc);line=dist(tangent,finish);
                }
                connections.push_back({{k,arc},{0,line}});
            }
            for(const auto& parts:connections){
                if(stop.stop_requested())break;
                double length=0;for(auto [curvature,part]:parts){(void)curvature;length+=part;}
                if(length<1e-6||std::abs(n.q.z-finish.z)>g*length+1e-7){++result.diagnostics.grade;continue;}
                auto old_size=nodes.size();std::size_t cursor=id;auto q=n.q;double heading=n.heading;bool allowed=true;double traversed=0;
                for(auto [curvature,part]:parts){
                    if(part<=1e-7)continue;
                    if(!turn_allowed(nodes[cursor],curvature)){allowed=false;break;}
                    auto count=static_cast<std::size_t>(std::ceil(part/step));double piece=part/count;
                    for(std::size_t i=0;i<count;++i){
                        if(stop.stop_requested()){allowed=false;break;}
                        if(++result.edges_checked>resources.max_edges||nodes.size()>=resources.max_states){budget=true;allowed=false;break;}
                        auto next=travel(q,heading,curvature,piece);traversed+=piece;next.z=std::lerp(n.q.z,finish.z,traversed/length);
                        auto z=ground(next);if(!z||!clear(q,next,cursor)){allowed=false;break;}
                        if(p.mode==DesignMode::surface)next.z=*z;
                        else if(*z-next.z>cut+2||next.z-*z>fill+2){++result.diagnostics.earthwork;allowed=false;break;}
                        Node child{next,wrap(heading+curvature*piece),nodes[cursor].cost+edge_cost(piece,*z-next.z),curvature,piece,cursor};initialize(child);nodes.push_back(child);cursor=nodes.size()-1;q=next;heading=nodes.back().heading;
                    }if(!allowed)break;
                }
                if(allowed&&dist(q,finish)<=1e-5){nodes.back().q=finish;if(rebuild(cursor)){goal=cursor;break;}}
                nodes.resize(old_size);
                if(budget||rebuilds>=road.max_ramp_candidates){budget=true;break;}
            }
            if(goal!=none||budget)break;
        }
        // In surface mode, 20 m primitives and nine fixed steering values can
        // jump across an entire feasible contour corridor. Adapt the primitive
        // length to the local slope and add steering that follows its grade
        // directions. Every primitive remains circular with radius >= R.
        double local_step=step;
        if(p.mode==DesignMode::surface)if(auto gradient=terrain.surface_gradient(n.q.x,n.q.y,false)){
            double slope=std::hypot((*gradient)[0],(*gradient)[1]);
            if(slope>g&&g>0)local_step=std::clamp(R*std::asin(std::min(1.0,g/slope)),2.0,step);
        }
        std::vector<double> curvatures{-1/R,-0.5/R,-0.25/R,-0.1/R,0.0,0.1/R,0.25/R,0.5/R,1/R};
        if(!guide.empty()){
            auto first=nearest_guide(n.q);
            for(double lookahead:{R,2*R,4*R}){auto target=first;
                while(target+1<guide.size()&&remaining[first]-remaining[target]<lookahead)++target;
                double d=dist(n.q,guide[target]);if(d>1e-6){double angle=std::atan2(guide[target].y-n.q.y,guide[target].x-n.q.x)-n.heading;
                    curvatures.push_back(std::clamp(2*std::sin(angle)/d,-1/R,1/R));}
            }
        }
        if(p.mode==DesignMode::surface){
            auto look=travel(n.q,n.heading,n.curvature,local_step);
            for(auto target:slope_headings(look)){
                double curvature=std::remainder(target-n.heading,2*pi)/local_step;
                if(std::abs(curvature)<=1/R+1e-12)curvatures.push_back(curvature);
            }
            curvatures.push_back(n.curvature);
        }
        std::sort(curvatures.begin(),curvatures.end());
        curvatures.erase(std::unique(curvatures.begin(),curvatures.end(),[](double a,double b){return std::abs(a-b)<1e-7;}),curvatures.end());
        for(double curvature:curvatures){
            if(!turn_allowed(n,curvature))continue;
            auto band=interval(n,curvature,local_step);if(!band)continue;auto q=travel(n.q,n.heading,curvature,local_step);auto z=ground(q);if(!z)continue;
            if(!clear(n.q,q,id))continue;
                double candidates[]{(*band)[0],(*band)[1],std::clamp(*z,(*band)[0],(*band)[1])};
            for(auto height:candidates){q.z=height;double exploration=terrain_weight*(local_step*std::abs(*z-q.z)+0.25*std::abs(curvature-n.curvature)*R);
                push({q,wrap(n.heading+curvature*local_step),n.cost+edge_cost(local_step,*z-q.z)+exploration,curvature,local_step,id});if(budget)break;}
            if(budget)break;
        }
    }
    result.status=stop.stop_requested()?SearchStatus::cancelled:(goal!=none?SearchStatus::found:(budget?SearchStatus::budget_exceeded:SearchStatus::no_path));return result;
}
SearchResult plan_continuous_ramp(const Terrain& terrain,Vec3 start,Vec3 finish,const Parameters& p,const UnitCosts& costs,
                                  const SearchOptions& resources,const RoadOptions& road,std::stop_token stop,ProgressCallback progress){
    if(progress)progress(0);
    if(stop.stop_requested()){SearchResult r;r.status=SearchStatus::cancelled;return r;}
    if(p.min_radius_m<=0||!std::isfinite(p.min_radius_m)||resources.xy_spacing_m<=0||!std::isfinite(resources.xy_spacing_m)||p.sample_spacing_m<=0||p.max_grade_percent<0)
        throw std::invalid_argument("Invalid continuous ramp parameters");
    if(p.max_grade_percent==0&&std::abs(start.z-finish.z)>1e-8){SearchResult r;r.status=SearchStatus::no_path;if(progress)progress(100);return r;}
    auto effective=p;effective.continuous_curves=true;
    if(costs.reusable_cut_fraction!=0)throw std::invalid_argument("Search requires reusable_cut_fraction=0 for additive costs");
    // Validate cost inputs before using them to rank search states.
    for(double v:{costs.road_per_m2,costs.soil_cut_per_m3,costs.rock_cut_per_m3,costs.fill_placement_per_m3,costs.imported_material_per_m3,costs.disposal_per_m3,costs.haul_per_m3_km,costs.rock_fraction,costs.internal_haul_km,costs.import_haul_km,costs.disposal_haul_km})
        if(!std::isfinite(v)||v<0)throw std::invalid_argument("Invalid cost input");
    if(costs.rock_fraction>1)throw std::invalid_argument("Invalid fractions or road width");
    SearchResult best;
    auto better=[](const SearchResult& a,const SearchResult& b){return b.status!=SearchStatus::found||a.estimate->total<b.estimate->total-1e-8||
        (std::abs(a.estimate->total-b.estimate->total)<=1e-8&&a.estimate->validation.metrics.horizontal_m<b.estimate->validation.metrics.horizontal_m);};
    std::vector<Vec3> direct_route{start,finish};
    if(p.mode==DesignMode::surface)if(auto profile=terrain.segment_profile(start,finish)){
        direct_route=std::move(*profile);direct_route.front()=start;direct_route.back()=finish;
    }
    auto direct=validate(direct_route,terrain,effective);
    const double direct_heading=std::atan2(finish.y-start.y,finish.x-start.x);
    auto matches=[&](std::optional<double> h){return !h||std::abs(std::remainder(direct_heading-*h,2*pi))<=1e-6;};
    if(direct.checked_constraints_pass()&&matches(road.start_heading)&&matches(road.finish_heading)){
        SearchResult r;r.strategy="continuous_ramp";r.status=SearchStatus::found;r.route=std::move(direct_route);
        r.effective_xy_spacing_m=resources.xy_spacing_m;r.effective_heading_step_degrees=road.heading_step_degrees;
        r.estimate=estimate_cost(r.route,terrain,effective,costs);if(road.trace)road.trace(r.route,true);
        // A straight with no earthwork charge attains the length lower bound.
        if(r.estimate->total<=r.estimate->road+1e-8){if(progress)progress(100);return r;}
        best=std::move(r);
    }
    bool seed_limited=false;std::vector<Vec3> corridor_guide;
    // Large surveyed TINs already contain useful bench/contour corridors. Use
    // them as a geometric seed before expanding a much larger pose lattice.
    if(terrain.faces().size()>5000&&!road.start_heading&&!road.finish_heading){
        auto seeded=rounded_surface_seed(terrain,start,finish,effective,costs,resources,road,stop);
        if(seeded.status==SearchStatus::cancelled)return seeded;
        seed_limited=seeded.status==SearchStatus::budget_exceeded;
        if(seeded.status!=SearchStatus::found)corridor_guide=std::move(seeded.reference_route);
        if(seeded.status==SearchStatus::found){
            if(road.first_feasible){if(progress)progress(100);return seeded;}
            if(better(seeded,best))best=std::move(seeded);
        }
    }
    if(!direct.checked_constraints_pass()){
        auto seeded=spiral_seed(terrain,start,finish,effective,costs,road,stop);
        if(seeded.status==SearchStatus::cancelled)return seeded;
        if(seeded.status==SearchStatus::found){
            if(road.first_feasible){if(progress)progress(100);return seeded;}
            if(better(seeded,best))best=std::move(seeded);
        }
    }
    SearchResult result;
    // Explore both objective-led and terrain-following candidates. The guidance
    // penalty never enters the final comparison of independently estimated costs.
    HeightField start_field(terrain,std::min(10.0,resources.xy_spacing_m)),finish_field(terrain,std::min(10.0,resources.xy_spacing_m));
    SearchDiagnostics totals;bool any_budget=seed_limited;int completed=0;if(progress)progress(5);
    const std::array<double,2> weights=corridor_guide.empty()||p.mode==DesignMode::earthwork?std::array<double,2>{0.0,0.04}:std::array<double,2>{0.04,0.0};
    for(double terrain_weight:weights)for(bool bottom_up:{false,true})for(double margin:{0.0,5.0}){
        if(p.mode==DesignMode::surface&&margin!=0){++completed;continue;}
        auto& field=((start.z<finish.z)!=bottom_up)?start_field:finish_field;
        auto attempt=continuous_attempt(terrain,start,finish,p,costs,resources,road,field,margin,bottom_up,terrain_weight,stop,
            progress?ProgressCallback([&](int value){progress(5+(completed*100+value)*95/800);}):ProgressCallback{},terrain_weight>0?std::span<const Vec3>(corridor_guide):std::span<const Vec3>{});
        totals+=attempt.diagnostics;any_budget|=attempt.status==SearchStatus::budget_exceeded;
        attempt.expanded+=result.expanded;attempt.edges_checked+=result.edges_checked;result=std::move(attempt);
        if(result.status==SearchStatus::cancelled)return result;
        ++completed;if(progress)progress(5+completed*95/8);
        if(result.status==SearchStatus::found&&better(result,best))best=result;
        if(road.first_feasible&&best.status==SearchStatus::found){best.expanded=result.expanded;best.edges_checked=result.edges_checked;best.diagnostics=totals;return best;}
    }
    if(best.status==SearchStatus::found){best.expanded=result.expanded;best.edges_checked=result.edges_checked;best.diagnostics=totals;return best;}
    result.diagnostics=totals;
    if(any_budget&&result.status!=SearchStatus::found)result.status=SearchStatus::budget_exceeded;
    return result;
}
}
