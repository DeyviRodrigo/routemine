#include "routemine/road_candidates.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <mutex>
#include <numbers>
#include <numeric>
#include <sstream>
#include <stdexcept>
namespace routemine {
namespace {
constexpr double pi=std::numbers::pi,rad=pi/180;
double dist(Vec3 a,Vec3 b){return std::hypot(a.x-b.x,a.y-b.y);}
bool same(Vec3 a,Vec3 b){return dist(a,b)<1e-6&&std::abs(a.z-b.z)<1e-6;}
double angle(Vec3 a,Vec3 b){return std::atan2(b.y-a.y,b.x-a.x);}
double wrap(double x){x=std::fmod(x,2*pi);return x<0?x+2*pi:x;}
double fraction(std::size_t n,std::size_t base){double value=0,scale=1;while(n){scale/=static_cast<double>(base);value+=scale*static_cast<double>(n%base);n/=base;}return value;}
Parameters unified(Parameters p){p.mode=DesignMode::earthwork;return p;}
bool better_connection(const SearchResult& candidate,const SearchResult& previous){
    const double a=candidate.estimate?candidate.estimate->total:0,b=previous.estimate?previous.estimate->total:0;
    if(a<b-1e-6)return true;if(a>b+1e-6)return false;
    return measure(candidate.route).horizontal_m<measure(previous.route).horizontal_m-1e-6;
}
void reverse_candidate(SearchResult& candidate){
    const auto n=candidate.route.size();std::reverse(candidate.route.begin(),candidate.route.end());std::reverse(candidate.curves.begin(),candidate.curves.end());
    for(auto& curve:candidate.curves){const auto first=curve.first;curve.first=n-1-curve.last;curve.last=n-1-first;curve.sweep_degrees=-curve.sweep_degrees;}
    candidate.estimate.reset();
}
bool profile(SearchResult& r,const Terrain& terrain,const Parameters& p,const UnitCosts& costs,bool fix_end){
    if(r.route.size()<2)return false;std::vector<double> lo(r.route.size()),hi(r.route.size()),ground(r.route.size());
    for(std::size_t i=0;i<r.route.size();++i){auto z=terrain.elevation(r.route[i].x,r.route[i].y);if(!z)return false;ground[i]=*z;lo[i]=*z-p.max_cut_m;hi[i]=*z+p.max_fill_m;}
    lo.front()=hi.front()=r.route.front().z;if(fix_end)lo.back()=hi.back()=r.route.back().z;
    const double g=p.max_grade_percent/100*0.999;
    for(std::size_t i=1;i<r.route.size();++i){double rise=g*dist(r.route[i-1],r.route[i]);lo[i]=std::max(lo[i],lo[i-1]-rise);hi[i]=std::min(hi[i],hi[i-1]+rise);if(lo[i]>hi[i]+1e-8)return false;}
    for(std::size_t i=r.route.size()-1;i>0;--i){double rise=g*dist(r.route[i-1],r.route[i]);lo[i-1]=std::max(lo[i-1],lo[i]-rise);hi[i-1]=std::min(hi[i-1],hi[i]+rise);if(lo[i-1]>hi[i-1]+1e-8)return false;}
    for(std::size_t i=1;i<r.route.size();++i){double rise=g*dist(r.route[i-1],r.route[i]),a=std::max(lo[i],r.route[i-1].z-rise),b=std::min(hi[i],r.route[i-1].z+rise);if(a>b+1e-8)return false;r.route[i].z=std::clamp(ground[i],a,std::max(a,b));}
    try{r.estimate=estimate_cost(r.route,terrain,p,costs,r.curves);}catch(const std::exception&){return false;}r.status=SearchStatus::found;return true;
}
struct Builder {
    const Terrain& terrain;const Parameters& p;std::stop_token stop;SearchResult result;
    std::function<void(std::span<const Vec3>)> exploratory;double gray_grade{},gray_length{};std::vector<Vec3> gray;
    Builder(const Terrain& t,const Parameters& params,Vec3 start,std::stop_token token,
        std::function<void(std::span<const Vec3>)> observer={},double max_grade=0):terrain(t),p(params),stop(token),exploratory(std::move(observer)),gray_grade(max_grade){result.route={start};result.strategy="continuous_ramp";result.effective_xy_spacing_m=p.sample_spacing_m;result.effective_heading_step_degrees=1;}
    void observe(std::span<const Vec3> ground){
        if(!exploratory||stop.stop_requested())return;
        for(std::size_t i=1;i<ground.size()&&!stop.stop_requested();++i){const auto a=ground[i-1],b=ground[i];const double h=dist(a,b);if(h<=1e-8)continue;
            // Profile vertices are exact face crossings, so the grade cannot
            // hide a steep face between two distant samples.
            if(100*std::abs(b.z-a.z)/h>gray_grade+1e-6){gray.clear();gray_length=0;continue;}
            if(gray.empty()||!same(gray.back(),a)){gray={a};gray_length=0;}
            const auto count=static_cast<std::size_t>(std::ceil(h/std::clamp(p.sample_spacing_m,0.25,2.)));
            if(count>p.max_samples){gray.clear();gray_length=0;continue;}
            auto previous=a;
            for(std::size_t j=1;j<=count&&!stop.stop_requested();++j){const double t=static_cast<double>(j)/count;Vec3 q=j==count?b:Vec3{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
                gray_length+=dist(previous,q);gray.push_back(q);previous=q;
                if(gray_length>=5){if(validate_grade_trace(terrain,gray,gray_grade,p.max_samples))exploratory(gray);gray={q};gray_length=0;}
            }
        }
    }
    bool append(Vec3 q){if(stop.stop_requested())return false;if(dist(result.route.back(),q)<1e-8)return true;auto ground=terrain.segment_profile(result.route.back(),q);if(!ground){gray.clear();gray_length=0;return false;}observe(*ground);for(auto x:*ground)if(dist(result.route.back(),x)>1e-8){if(result.route.size()>=p.max_samples)return false;result.route.push_back(x);}return true;}
    bool arc(Vec3 begin,double heading,Vec3 end){double x=end.x-begin.x,y=end.y-begin.y,den=2*(std::cos(heading)*y-std::sin(heading)*x);
        if(std::abs(den)<1e-10)return append(end);double radius=(x*x+y*y)/den;if(!std::isfinite(radius)||std::abs(radius)<p.min_radius_m-1e-6)return false;
        Vec3 center{begin.x-radius*std::sin(heading),begin.y+radius*std::cos(heading),0};double first=angle(center,begin),last=angle(center,end),turn=std::copysign(wrap(std::copysign(1.,radius)*(last-first)),radius);
        if(std::abs(turn)>pi+1e-8)return false;auto start=result.route.size()-1;
        double count_d=std::ceil(std::max(std::abs(turn)/rad,std::abs(radius*turn)/p.sample_spacing_m));if(!std::isfinite(count_d)||count_d>p.max_samples)return false;
        auto count=static_cast<std::size_t>(std::max(1.,count_d));for(std::size_t i=1;i<=count;++i){double a=first+turn*static_cast<double>(i)/count;if(!append(i==count?end:Vec3{center.x+std::abs(radius)*std::cos(a),center.y+std::abs(radius)*std::sin(a),0})){result.route.resize(start+1);return false;}}
        result.curves.push_back({start,result.route.size()-1,center,turn/rad});return true;
    }
    bool biarc(Vec3 previous,double h,Vec3 q,double next){double ux=std::cos(h),uy=std::sin(h),vx=std::cos(next),vy=std::sin(next),dx=q.x-previous.x,dy=q.y-previous.y;
        double A=2*(1-ux*vx-uy*vy),B=dx*(ux+vx)+dy*(uy+vy),C=dx*dx+dy*dy,disc=B*B+A*C;
        if(disc<0||(!std::isfinite(disc)))return false;double d=std::abs(A)<1e-10?(std::abs(B)>1e-10?C/(2*B):0):(-B+std::sqrt(disc))/A;
        if(!std::isfinite(d)||d<=0)return false;Vec3 middle{(previous.x+q.x+d*(ux-vx))/2,(previous.y+q.y+d*(uy-vy))/2,0};
        auto old=result.route.size();auto curves=result.curves.size();if(!arc(previous,h,middle))return false;
        double ex=middle.x-q.x,ey=middle.y-q.y,den=2*(-vx*ey+vy*ex),radius=(ex*ex+ey*ey)/den;
        if(!std::isfinite(radius)||std::abs(radius)<p.min_radius_m-1e-6){result.route.resize(old);result.curves.resize(curves);return false;}
        Vec3 center{q.x+radius*vy,q.y-radius*vx,0};double mh=angle(center,middle)-std::copysign(pi/2,radius);
        if(!arc(middle,mh,q)){result.route.resize(old);result.curves.resize(curves);return false;}return true;
    }
};
std::vector<double> stations(std::span<const Vec3> path){std::vector<double> result(path.size());for(std::size_t i=1;i<path.size();++i)result[i]=result[i-1]+dist(path[i-1],path[i]);return result;}
Vec3 at(std::span<const Vec3> path,const std::vector<double>& station,double s){auto it=std::lower_bound(station.begin(),station.end(),s);if(it==station.begin())return path.front();if(it==station.end())return path.back();auto i=static_cast<std::size_t>(it-station.begin());double t=(s-station[i-1])/(station[i]-station[i-1]);return {std::lerp(path[i-1].x,path[i].x,t),std::lerp(path[i-1].y,path[i].y,t),std::lerp(path[i-1].z,path[i].z,t)};}
void gaps(DesignReview& review,std::span<const Vec3> path,const std::vector<double>& station){review.gaps.clear();if(review.connected)return;std::vector<std::pair<double,double>> covered;for(const auto& s:review.sections)covered.push_back({s.first_station_m,s.last_station_m});std::sort(covered.begin(),covered.end());double cursor=0;
    auto add=[&](double a,double b){if(b-a>1e-5)review.gaps.push_back({a,b,at(path,station,a),at(path,station,b),"Tramo pendiente; se siguen explorando rodeos y curvas."});};for(auto [a,b]:covered){add(cursor,a);cursor=std::max(cursor,b);}add(cursor,station.back());}
}
bool validate_grade_trace(const Terrain& terrain,std::span<const Vec3> route,double max_grade,std::size_t max_samples){
    if(route.size()<2||route.size()>max_samples||!std::isfinite(max_grade)||max_grade<0)return false;
    std::size_t samples=0;
    try{for(std::size_t i=0;i<route.size();++i){const auto q=route[i];if(!std::isfinite(q.x)||!std::isfinite(q.y)||!std::isfinite(q.z))return false;
            auto z=terrain.elevation(q.x,q.y);if(!z||std::abs(*z-q.z)>1e-6)return false;
            if(i==0)continue;const auto a=route[i-1];const double length=dist(a,q);if(length<=1e-8)return false;
            auto ground=terrain.segment_profile(a,q,max_grade);if(!ground||ground->size()>max_samples-samples)return false;samples+=ground->size();
            // The rendered chord must stay on the TIN between its endpoints.
            // A low-grade ridge still needs an explicit face-crossing vertex.
            for(const auto vertex:*ground){const double t=std::clamp(dist(a,vertex)/length,0.,1.);
                if(std::abs(vertex.z-std::lerp(a.z,q.z,t))>1e-6)return false;}
            for(std::size_t j=1;j<ground->size();++j){double h=dist((*ground)[j-1],(*ground)[j]);if(h<=1e-8){if(std::abs((*ground)[j].z-(*ground)[j-1].z)>1e-6)return false;continue;}
                if(100*std::abs((*ground)[j].z-(*ground)[j-1].z)/h>max_grade+1e-6)return false;}
        }return true;
    }catch(const std::exception&){return false;}
}
bool validate_road_candidate(const Terrain& terrain,const SearchResult& candidate,const Parameters& original,const RoadCandidateLimits& limits){
    if(!std::isfinite(limits.max_intervention_length_m)||limits.max_intervention_length_m<=0||!std::isfinite(limits.max_surface_grade_factor)||limits.max_surface_grade_factor<1||limits.max_surface_grade_factor>2)return false;
    if(candidate.status!=SearchStatus::found||candidate.route.size()<2||!candidate.local_sections.empty())return false;const auto p=unified(original);
    try{
        if(!validate(candidate.route,terrain,p,candidate.curves).checked_constraints_pass())return false;
        double exceptional=0;
        for(std::size_t i=1;i<candidate.route.size();++i){auto ground=terrain.segment_profile(candidate.route[i-1],candidate.route[i]);if(!ground)return false;
            for(std::size_t j=1;j<ground->size();++j){auto a=(*ground)[j-1],b=(*ground)[j];double h=dist(a,b);if(h<=1e-8)continue;double grade=100*std::abs(b.z-a.z)/h;
                if(grade>p.max_grade_percent*limits.max_surface_grade_factor+1e-6)return false;
                if(grade>p.max_grade_percent+1e-6)exceptional+=h;else exceptional=0;
                if(exceptional>limits.max_intervention_length_m+1e-6)return false;
            }
        }
        double intervention=0;for(const auto& segment:earthwork_segments(terrain,candidate.route,p.surface_tolerance_m)){
            if(segment.kind==EarthworkKind::surface)intervention=0;else intervention+=dist(segment.first,segment.last);
            if(intervention>limits.max_intervention_length_m+1e-6)return false;
        }
        return true;
    }catch(const std::exception&){return false;}
}
DesignReview explore_road_candidates(const Terrain& terrain,const SearchResult& reference,const Parameters& original,const UnitCosts& costs,const SearchOptions& resources,const RoadOptions& options,const RoadCandidateLimits& limits,std::stop_token stop,
    std::function<void(const DesignReview&,std::size_t)> update,std::function<void(const SearchResult&)> branch,std::function<void(std::span<const Vec3>)> exploratory){
    if(original.min_radius_m<=0||!std::isfinite(original.min_radius_m))throw std::invalid_argument("Se requiere un radio mínimo positivo para generar carreteras curvas.");
    (void)measure(reference.route);auto p=unified(original);const bool guide_only=reference.strategy=="candidate_guide";const auto station=stations(reference.route);const double total=station.back(),minimum=std::max({2*p.min_radius_m,p.min_straight_m,4*p.road_width_m,40.});DesignReview review;
    const auto bounds=terrain.bounds();Vec3 lowcenter{};double weight=0;for(auto q:terrain.vertices()){double w=std::pow(std::max(0.,bounds[1].z-q.z),2);lowcenter.x+=w*q.x;lowcenter.y+=w*q.y;weight+=w;}if(weight>0){lowcenter.x/=weight;lowcenter.y/=weight;}else lowcenter={std::midpoint(bounds[0].x,bounds[1].x),std::midpoint(bounds[0].y,bounds[1].y),0};
    auto publish=[&](std::size_t round){gaps(review,reference.route,station);std::ostringstream text;text<<"Alternativas viales: ronda "<<round<<". Rectas y curvas tangentes; radios y separación del diseño original.\n"<<"Pendiente de carretera ≤ "<<p.max_grade_percent<<" %; terreno ≤ "<<p.max_grade_percent*limits.max_surface_grade_factor<<" % y excesos/intervenciones continuos ≤ "<<limits.max_intervention_length_m<<" m.\n"<<review.sections.size()<<" alternativas ancladas, con longitudes distintas; no se eliminan por ser largas.\n";review.report=text.str();if(update)update(review,round);};
    auto emit=[&](SearchResult candidate,bool fix_end){if(stop.stop_requested()||candidate.route.size()<2||measure(candidate.route).horizontal_m<minimum)return false;
        if(!profile(candidate,terrain,p,costs,fix_end)||!validate_road_candidate(terrain,candidate,p,limits))return false;
        if(branch)branch(candidate);if(options.trace)options.trace(candidate.route,true);return true;};
    auto accept=[&](SearchResult candidate,double first,double last){if(stop.stop_requested())return false;
        const bool complete=first<1e-6&&last>total-1e-6;
        if(complete&&(!same(candidate.route.front(),reference.route.front())||!same(candidate.route.back(),reference.route.back())))return false;
        if(complete){candidate.route.front()=reference.route.front();candidate.route.back()=reference.route.back();}
        if(!profile(candidate,terrain,p,costs,true)||!validate_road_candidate(terrain,candidate,p,limits))return false;
        auto next=review;const bool improved=complete&&(!next.connected||better_connection(candidate,*next.connected));
        if(improved)next.connected=candidate;
        const bool duplicate=std::any_of(review.sections.begin(),review.sections.end(),[&](const DesignSection& old){return old.candidate.route.size()==candidate.route.size()&&std::equal(old.candidate.route.begin(),old.candidate.route.end(),candidate.route.begin(),same);});
        if(duplicate&&!improved)return false;
        if(guide_only&&!complete){if(branch)branch(candidate);if(options.trace)options.trace(candidate.route,true);return true;}
        if(!duplicate&&next.sections.size()<128)next.sections.push_back({candidate,first,last});gaps(next,reference.route,station);
        if(!validate_design_review(terrain,next,p))return false;review=std::move(next);if(branch)branch(candidate);if(options.trace)options.trace(candidate.route,true);return true;};
    auto connect_prefix=[&](const Builder& prefix,double heading){
        if(stop.stop_requested()||prefix.result.route.size()<2)return false;
        const bool from_a=same(prefix.result.route.front(),reference.route.front()),from_b=same(prefix.result.route.front(),reference.route.back());
        if(!from_a&&!from_b)return false;const auto goal=from_a?reference.route.back():reference.route.front();
        const auto current=prefix.result.route.back();
        // A real forward tangent separates opposite-sign turns. The final
        // circular arc uses the analytic incoming pose, never a sampled chord.
        for(double scale:{1.,3.}){if(stop.stop_requested())return false;
            Builder trial(terrain,p,prefix.result.route.front(),stop,exploratory,2*original.max_grade_percent);trial.result=prefix.result;
            const double run=scale*std::max({p.min_straight_m,p.min_straight_same_direction_m,2*p.road_width_m,1.});
            Vec3 tangent{current.x+run*std::cos(heading),current.y+run*std::sin(heading),0};
            if(!trial.append(tangent)||!trial.arc(tangent,heading,goal))continue;
            trial.result.route.back()=goal;trial.result.strategy="connected_continuation";
            if(from_b)reverse_candidate(trial.result);
            if(accept(std::move(trial.result),0,total))return true;
        }return false;
    };
    auto road=options;road.trace={};road.adaptive_resolution=false;road.first_feasible=true;road.propose_earthwork=false;road.max_ramp_candidates=std::min<std::size_t>(16,options.max_ramp_candidates);
    auto search_limits=resources;search_limits.max_states=std::min<std::size_t>(resources.max_states,6000);search_limits.max_expansions=std::min<std::size_t>(resources.max_expansions,700);search_limits.max_edges=std::min<std::size_t>(resources.max_edges,14000);
    std::condition_variable_any wake;std::mutex mutex;std::size_t round=0;publish(round);
    while(!stop.stop_requested()){
        ++round;
        auto grow=[&](std::size_t initial,std::size_t cap){auto value=std::min(initial,cap);auto levels=std::min<std::size_t>(round/8,64);while(levels--&&value<cap){if(value>=cap-value)return cap;value*=2;}return value;};
        search_limits.max_states=grow(6000,resources.max_states);search_limits.max_expansions=grow(700,resources.max_expansions);search_limits.max_edges=grow(14000,resources.max_edges);
        road.max_ramp_candidates=grow(12,options.max_ramp_candidates);
        // Endpoint-anchored switchbacks get a connection attempt every round.
        // Their parallel legs have true tangent semicircles, not angular
        // reversals; both the grade trace and complete road remain checked.
        const auto start_anchor=reference.route.front(),finish_anchor=reference.route.back();
        const double anchor_distance=dist(start_anchor,finish_anchor),axis=angle(start_anchor,finish_anchor);
        const auto max_turns=static_cast<std::size_t>(std::min(16.,std::floor(anchor_distance/(2*std::max(p.min_radius_m,p.road_width_m)))));
        if(max_turns>0){const auto turns=1+(round-1)%max_turns;const double radius=anchor_distance/(2*turns);
            const double run=std::max(p.min_straight_m,2*p.road_width_m)+std::max(2*radius,anchor_distance*.25)*(1+.25*fraction(round,19));
            Builder serpentine(terrain,p,start_anchor,stop,exploratory,2*original.max_grade_percent);serpentine.result.strategy="serpentine";
            Vec3 current=start_anchor;double heading=axis-pi/2;bool built=true;
            for(std::size_t turn=0;turn<turns&&!stop.stop_requested();++turn){Vec3 tangent{current.x+run*std::cos(heading),current.y+run*std::sin(heading),0};
                if(!serpentine.append(tangent)){built=false;break;}
                const double sign=turn%2?-1.:1.;Vec3 end{tangent.x-2*sign*radius*std::sin(heading),tangent.y+2*sign*radius*std::cos(heading),0};
                if(!serpentine.arc(tangent,heading,end)){built=false;break;}current=end;heading+=sign*pi;
                emit(serpentine.result,false);
            }
            if(built&&!stop.stop_requested()){
                if(turns%2&&!serpentine.append(finish_anchor))built=false;
                if(built&&dist(serpentine.result.route.back(),finish_anchor)<1e-6){serpentine.result.route.back()=finish_anchor;accept(std::move(serpentine.result),0,total);}
            }
        }
        // A direct connection is a candidate too; retaining longer alternatives
        // must not prevent a later cheaper/shorter incumbent from replacing it.
        if(!stop.stop_requested()){Builder direct(terrain,p,start_anchor,stop,exploratory,2*original.max_grade_percent);
            if(direct.append(finish_anchor)){direct.result.route.back()=finish_anchor;direct.result.strategy="direct_connection";accept(std::move(direct.result),0,total);}}
        double first=0,last=total;if(round%3){double span=std::min(total,minimum*(2+10*fraction(round,3)));first=(total-span)*fraction(round,5);last=first+span;}
        auto a=at(reference.route,station,first),b=at(reference.route,station,last);
        if(guide_only){auto za=terrain.elevation(a.x,a.y),zb=terrain.elevation(b.x,b.y);
            if(!za||!zb){a=reference.route.front();b=reference.route.back();first=0;last=total;za=terrain.elevation(a.x,a.y);zb=terrain.elevation(b.x,b.y);}
            if(za)a.z=*za;if(zb)b.z=*zb;
        }
        const double rotation=2*pi*fraction(round,7),ex=1.,ey=0.65+0.7*fraction(round,11);
        Vec3 center=round%3==0?Vec3{std::midpoint(bounds[0].x,bounds[1].x),std::midpoint(bounds[0].y,bounds[1].y),0}:lowcenter;
        const double extent=dist(bounds[0],bounds[1]);if(round%4){center.x+=extent*0.08*std::cos(rotation);center.y+=extent*0.08*std::sin(rotation);}
        auto local=[&](Vec3 q){double x=q.x-center.x,y=q.y-center.y;return Vec3{(x*std::cos(rotation)+y*std::sin(rotation))/ex,(-x*std::sin(rotation)+y*std::cos(rotation))/ey,0};};
        auto world=[&](double r,double theta){double x=ex*r*std::cos(theta),y=ey*r*std::sin(theta);return Vec3{center.x+x*std::cos(rotation)-y*std::sin(rotation),center.y+x*std::sin(rotation)+y*std::cos(rotation),0};};
        auto aa=local(a),bb=local(b);double ra=std::hypot(aa.x,aa.y),rb=std::hypot(bb.x,bb.y);
        for(double sign:{-1.,1.})if(std::min(ra,rb)>p.min_radius_m*1.1&&!stop.stop_requested()){
            int turns=static_cast<int>(1+(round/3)%12);double start=std::atan2(aa.y,aa.x),end=std::atan2(bb.y,bb.x),sweep=sign*(wrap(sign*(end-start))+2*pi*turns),k=std::log(rb/ra)/sweep;
            auto tangent=[&](double theta){double x=ex*(k*std::cos(theta)-std::sin(theta))*sign,y=ey*(k*std::sin(theta)+std::cos(theta))*sign;return std::atan2(x*std::sin(rotation)+y*std::cos(rotation),x*std::cos(rotation)-y*std::sin(rotation));};
            Builder builder(terrain,p,a,stop,exploratory,2*original.max_grade_percent);auto previous=a;double previous_heading=tangent(start);
            const double approximate=std::abs(sweep)*std::max(ra,rb)*std::max(ex,ey);
            const auto count=static_cast<std::size_t>(std::ceil(std::max(std::abs(sweep)/(5*rad),approximate/25.)));
            std::size_t emitted=0;double since_check=0;
            for(std::size_t i=1;i<=count&&!stop.stop_requested();++i){double t=static_cast<double>(i)/count,theta=start+sweep*t;Vec3 q=i==count?b:world(ra*std::exp(std::log(rb/ra)*t),theta);double h=tangent(theta);
                if(!builder.biarc(previous,previous_heading,q,h))break;since_check+=dist(previous,q);previous=q;previous_heading=h;
                if(since_check>=minimum&&emitted<4){if(emit(builder.result,false))++emitted;since_check=0;}
                if(i==count){builder.result.route.front()=a;builder.result.route.back()=b;accept(builder.result,first,last);}
            }
            if(emitted==0)emit(builder.result,false);
        }
        // Terrain-guided primitive growth: each deviation is a tangent arc, not
        // a straight ray snapped back to the anchor. The next heading follows
        // the local contour; the accepted prefix is independently profiled.
        Vec3 origin;
        const auto sector=exploratory?round%6:round%4;
        if(sector==1)origin=reference.route.front();
        else if(sector==2)origin=reference.route.back();
        else if(sector==3)origin=at(reference.route,station,total*fraction(round,13));
        else{
            origin=at(reference.route,station,total*fraction(round,19));
            for(std::size_t attempt=0;attempt<16;++attempt){auto sample=round+attempt;
                Vec3 q{std::lerp(bounds[0].x,bounds[1].x,fraction(sample,13)),std::lerp(bounds[0].y,bounds[1].y,fraction(sample,17)),0};
                if(auto z=terrain.elevation(q.x,q.y)){q.z=*z;origin=q;break;}
            }
        }
        if(guide_only){if(auto z=terrain.elevation(origin.x,origin.y))origin.z=*z;else origin=round%2?reference.route.front():reference.route.back();}
        double heading=rotation;
        auto contour_heading=[&](const std::array<double,2>& gradient,double current){double a=std::atan2(gradient[1],gradient[0])+pi/2,b=a+pi;
            return std::abs(std::remainder(a-current,2*pi))<=std::abs(std::remainder(b-current,2*pi))?a:b;};
        const double normal_grade=p.max_grade_percent/100,exploration_grade=exploratory?2*normal_grade:normal_grade;
        // Alternate level contours with uphill/downhill traverses, including
        // the terrain-only band between the normal and doubled grade limits.
        // These headings guide exploration; they never relax road validation.
        const std::array<double,5> grade_families{0,1.6,-1.6,.65,-.65};
        const double desired_grade=exploratory?normal_grade*grade_families[(round-1)%grade_families.size()]:0;
        auto terrain_heading=[&](const std::array<double,2>& gradient,double current,double traveled){
            const double slope=std::hypot(gradient[0],gradient[1]);if(slope<=1e-8)return current;
            if(desired_grade==0)return contour_heading(gradient,current);
            const double requested=std::clamp(desired_grade+normal_grade*.12*std::sin(traveled/std::max(600.,12*p.min_radius_m)),-.9*exploration_grade,.9*exploration_grade);
            const double offset=std::acos(std::clamp(requested/slope,-1.,1.)),up=std::atan2(gradient[1],gradient[0]);
            const double a=up+offset,b=up-offset;
            return std::abs(std::remainder(a-current,2*pi))<=std::abs(std::remainder(b-current,2*pi))?a:b;
        };
        if(auto gradient=terrain.surface_gradient(origin.x,origin.y,false);gradient)heading=terrain_heading(*gradient,heading,0);
        Builder growth(terrain,p,origin,stop,exploratory,2*original.max_grade_percent);auto current=origin;std::size_t last_good=0,connection_attempts=0;double since_check=0,last_sign=0,straight_run=0,traveled=0;
        const auto growth_steps=exploratory?128+32*std::min<std::size_t>(12,round/4):64;
        for(std::size_t i=0;i<growth_steps&&!stop.stop_requested();++i){double target=heading,slope=0;
            if(auto gradient=terrain.surface_gradient(current.x,current.y,false);gradient){slope=std::hypot((*gradient)[0],(*gradient)[1]);
                if(slope>1e-8){double direction=terrain_heading(*gradient,heading,traveled);target=heading+std::remainder(direction-heading,2*pi);}}
            double length=std::max(5.,std::min(exploratory?40.:25.,p.min_radius_m*0.35*(exploratory?.8+fraction(round,17):1.)));
            if(slope>exploration_grade&&exploration_grade>0)length=std::clamp(p.min_radius_m*std::asin(std::min(1.,exploration_grade/slope)),2.,length);
            double curvature=std::clamp(std::remainder(target-heading,2*pi)/length,-1/std::max(1.,p.min_radius_m),1/std::max(1.,p.min_radius_m));
            // A straight tangent on a sloping face is a useful contour road.
            // Do not force it off that contour merely to make the display curved.
            if(slope<=1e-8&&std::abs(curvature)<1e-7)curvature=(round%2?1:-1)/(p.min_radius_m*(3+round%6)*(exploratory?1+traveled/std::max(300.,10*p.min_radius_m):1.));
            if(std::abs(curvature)>1e-10){
                const bool positive_tangent=straight_run>1e-6,reversal=curvature*last_sign<0;
                const double required=positive_tangent&&curvature*last_sign>0?std::max(p.min_straight_m,p.min_straight_same_direction_m):p.min_straight_m;
                // Initial and reversal tangents use S; a positive tangent
                // between same-sign curves uses O. Compound arcs need no
                // artificial tangent when their shared advance is zero.
                if((positive_tangent||reversal)&&straight_run+1e-8<required)curvature=0;
            }
            double next_heading=heading+curvature*length;Vec3 next;
            if(std::abs(curvature)<1e-10)next={current.x+length*std::cos(heading),current.y+length*std::sin(heading),0};
            else next={current.x+(std::sin(next_heading)-std::sin(heading))/curvature,current.y+(std::cos(heading)-std::cos(next_heading))/curvature,0};
            const bool appended=std::abs(curvature)<1e-10?growth.append(next):growth.arc(current,heading,next);
            if(!appended)break;current=next;heading=next_heading;since_check+=length;traveled+=length;
            if(std::abs(curvature)<1e-10)straight_run+=length;else{last_sign=std::copysign(1.,curvature);straight_run=0;}
            if(connection_attempts<2&&traveled>=minimum*(2+2*connection_attempts)){
                connect_prefix(growth,heading);++connection_attempts;
            }
            const double check_interval=exploratory?std::max(minimum,std::min(1000.,traveled*.2)):minimum;
            if(since_check>=check_interval){if(emit(growth.result,false))last_good=growth.result.route.size();else if(last_good&&!exploratory)break;since_check=0;}
        }
        if(last_good==0)emit(growth.result,false);
        if(round%3==0&&!stop.stop_requested())try{auto pose=road;pose.start_heading.reset();pose.finish_heading=angle(a,b);auto candidate=plan_continuous_ramp(terrain,a,b,p,costs,search_limits,pose,stop);if(candidate.status==SearchStatus::found)accept(std::move(candidate),first,last);}catch(const std::exception&){}
        publish(round);if(!stop.stop_requested()){std::unique_lock lock(mutex);wake.wait_for(lock,stop,std::chrono::milliseconds(100),[]{return false;});}
    }
    publish(round);return review;
}
}
