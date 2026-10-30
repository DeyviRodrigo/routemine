#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace routemine {
namespace {
constexpr double pi=std::numbers::pi, rad=pi/180;
constexpr auto none=std::numeric_limits<std::size_t>::max();
double hdist(Vec3 a,Vec3 b){return std::hypot(b.x-a.x,b.y-a.y);}
double bearing(Vec3 a,Vec3 b){return std::atan2(b.y-a.y,b.x-a.x);}
double wrap(double a){a=std::fmod(a,2*pi);return a<0?a+2*pi:a;}
bool aligned(double a,double b){return std::abs(std::remainder(a-b,2*pi))<1e-6;}
Vec3 advance(Vec3 p,double heading,double length){return {p.x+length*std::cos(heading),p.y+length*std::sin(heading),p.z};}
Vec3 center(Vec3 p,double heading,double radius,double sign){return advance(p,heading+sign*pi/2,radius);}
Vec3 arc_end(Vec3 p,double heading,double radius,double turn){
    if(std::abs(turn)<1e-10)return p;
    auto c=center(p,heading,radius,std::copysign(1.0,turn));auto a=bearing(c,p)+turn;
    return {c.x+radius*std::cos(a),c.y+radius*std::sin(a),p.z};
}
struct Key {long long x,y;int heading;bool operator==(const Key&)const=default;};
struct Hash {std::size_t operator()(Key k)const{return std::hash<long long>{}(k.x)^(std::hash<long long>{}(k.y)<<1)^(std::hash<int>{}(k.heading)<<2);}};
double point_segment(Vec3 p,Vec3 a,Vec3 b){auto d=to_local(b,a),q=to_local(p,a);double t=std::clamp((q.x*d.x+q.y*d.y)/(d.x*d.x+d.y*d.y),0.0,1.0);return std::hypot(q.x-t*d.x,q.y-t*d.y);}
double cross(Vec3 a,Vec3 b,Vec3 c){return (b.x-a.x)*(c.y-a.y)-(b.y-a.y)*(c.x-a.x);}
double segment_distance(Vec3 a,Vec3 b,Vec3 c,Vec3 d){
    if(std::min(a.x,b.x)<=std::max(c.x,d.x)&&std::min(c.x,d.x)<=std::max(a.x,b.x)&&std::min(a.y,b.y)<=std::max(c.y,d.y)&&std::min(c.y,d.y)<=std::max(a.y,b.y)&&
       cross(a,b,c)*cross(a,b,d)<=0&&cross(c,d,a)*cross(c,d,b)<=0)return 0;
    return std::min({point_segment(a,c,d),point_segment(b,c,d),point_segment(c,a,b),point_segment(d,a,b)});
}
void spacing_check(std::span<const Vec3> route,const Parameters& p,Validation& v){
    double width=std::max(0.001,p.road_width_m),step=std::clamp(width/2,1.0,5.0),side=std::max(width,step);
    std::vector<Vec3> points{route.front()};std::vector<double> station{0};double s=0,next=step;
    for(std::size_t i=1;i<route.size();++i){double h=hdist(route[i-1],route[i]);while(next<s+h){
            if(points.size()>=p.max_samples){validation_issue(v,route,"ramp_spacing_budget",i-1,i,"Separación entre vueltas: se agotó el presupuesto de muestras (máximo "+std::to_string(p.max_samples)+"). La comprobación quedó incompleta.");return;}
            double t=(next-s)/h;points.push_back({std::lerp(route[i-1].x,route[i].x,t),std::lerp(route[i-1].y,route[i].y,t),0});station.push_back(next);next+=step;}s+=h;}
    if(hdist(points.back(),route.back())>1e-7){points.push_back(route.back());station.push_back(s);}
    std::unordered_map<Key,std::vector<std::size_t>,Hash> cells;
    auto cell=[&](double value,double origin){return static_cast<long long>(std::floor((value-origin)/side));};
    for(std::size_t i=1;i<points.size();++i){auto a=points[i-1],b=points[i];
        auto x0=cell(std::min(a.x,b.x)-width,points[0].x),x1=cell(std::max(a.x,b.x)+width,points[0].x),y0=cell(std::min(a.y,b.y)-width,points[0].y),y1=cell(std::max(a.y,b.y)+width,points[0].y);
        for(auto y=y0;y<=y1;++y)for(auto x=x0;x<=x1;++x){auto found=cells.find({x,y,0});if(found==cells.end())continue;
            for(auto j:found->second){if(station[i-1]-station[j]<std::max(3*width,2*step))continue;
                if(segment_distance(a,b,points[j-1],points[j])<width-1e-5){
                    auto segment=[&](double distance){double total=0;for(std::size_t k=1;k<route.size();++k){total+=hdist(route[k-1],route[k]);if(total>=distance-1e-8)return k;}return route.size()-1;};
                    auto first=segment(station[i-1]),last=segment(station[i]),other_first=segment(station[j-1]),other_last=segment(station[j]);
                    validation_issue(v,route,"ramp_spacing_or_intersection",first-1,last,
                        "Cruce o separación insuficiente respecto al tramo de vértices "+std::to_string(other_first)+"–"+std::to_string(other_last+1)+
                        ": distancia "+std::to_string(segment_distance(a,b,points[j-1],points[j]))+" m; mínimo exigido: "+std::to_string(width)+" m.");return;}}}
        x0=cell(std::min(a.x,b.x),points[0].x);x1=cell(std::max(a.x,b.x),points[0].x);y0=cell(std::min(a.y,b.y),points[0].y);y1=cell(std::max(a.y,b.y),points[0].y);
        for(auto y=y0;y<=y1;++y)for(auto x=x0;x<=x1;++x)cells[{x,y,0}].push_back(i);
    }
}
}
bool alignment_enabled(const Parameters& p){return p.continuous_curves||p.min_radius_m>0||p.min_straight_m>0||p.min_straight_same_direction_m>0||p.max_turn_degrees<180;}

// Recheck saved XYZ and arc metadata, including tangent joins. Face crossings on a
// chord are allowed inside its circular envelope; arcs use chords of at most 1 degree.
void validate_alignment(std::span<const Vec3> route,std::span<const AlignmentCurve> curves,const Parameters& p,Validation& v) {
    if(!alignment_enabled(p)&&curves.empty())return;
    std::size_t issue_first=0,issue_last=route.size()-1;double required_straight=p.min_straight_m;
    auto fail=[&](const char* code){
        if(std::find(v.violations.begin(),v.violations.end(),code)!=v.violations.end())return;
        std::string description;
        const std::string key=code;
        if(key=="alignment_metadata")description="Datos de curva inválidos: revise los índices, el centro y el ángulo del arco.";
        else if(key=="straight_before_turn")description="Tramo recto de avance demasiado corto: longitud "+std::to_string(hdist(route[issue_first],route[issue_last]))+" m; mínimo: "+std::to_string(p.min_straight_m)+" m. La longitud de las curvas no cuenta como recta.";
        else if(key=="straight_same_direction")description="Recta entre curvas del mismo sentido demasiado corta: longitud "+std::to_string(hdist(route[issue_first],route[issue_last]))+" m; mínimo: "+std::to_string(required_straight)+" m. Se mide la recta completa entre los arcos, sin sumar sus longitudes.";
        else if(key=="unmodelled_turn")description="Cambio de dirección sin una curva definida, o retroceso dentro de una recta.";
        else if(key=="curve_radius")description="Radio de curva inferior al mínimo permitido: "+std::to_string(p.min_radius_m)+" m.";
        else if(key=="max_turn_angle")description="Ángulo de giro superior al máximo permitido: "+std::to_string(p.max_turn_degrees)+" grados.";
        else if(key=="curve_tangency")description="Empalme sin tangencia entre la curva y el tramo contiguo.";
        else description="Forma del arco incompatible con su centro, radio, sentido o ángulo declarado.";
        validation_issue(v,route,code,issue_first,issue_last,description);
    };
    auto straight=[&](std::size_t first,std::size_t last,bool minimum,bool same_direction=false){
        issue_first=first;issue_last=last;
        if(first>last||last>=route.size()){fail("alignment_metadata");return;}
        double length=hdist(route[first],route[last]);
        // A tangent is the whole collinear block between curves or an endpoint,
        // regardless of the number of TIN crossings or sampled vertices in it.
        // Contiguous same-direction arcs have no tangent to measure; a reversal
        // still requires one even when its two arcs share a vertex.
        const bool require_minimum=minimum||(p.continuous_curves&&length>1e-6);
        required_straight=same_direction&&length>1e-6?std::max(p.min_straight_m,p.min_straight_same_direction_m):p.min_straight_m;
        if((require_minimum||(same_direction&&length>1e-6))&&length+1e-6<required_straight)
            fail(required_straight>p.min_straight_m?"straight_same_direction":"straight_before_turn");
        if(first==last)return;
        double heading=bearing(route[first],route[last]),progress=-1e-6;
        for(auto i=first;i<=last;++i){auto q=to_local(route[i],route[first]);double s=q.x*std::cos(heading)+q.y*std::sin(heading);
            if(std::abs(-q.x*std::sin(heading)+q.y*std::cos(heading))>1e-5||s<progress-1e-6||s>length+1e-5)fail("unmodelled_turn");progress=s;}
    };
    std::size_t cursor=0;
    std::optional<double> exit_heading;double previous_sign=0;
    for(const auto& c:curves) {
        issue_first=c.first;issue_last=c.last;
        if(c.first<cursor||c.first>=c.last||c.last>=route.size()||!finite(c.center)||!std::isfinite(c.sweep_degrees)||std::abs(c.sweep_degrees)<1e-6||std::abs(c.sweep_degrees)>180+1e-8) {fail("alignment_metadata");return;}
        straight(cursor,c.first,!p.continuous_curves||(previous_sign!=0&&previous_sign*c.sweep_degrees<0),previous_sign*c.sweep_degrees>0);
        issue_first=c.first;issue_last=c.last;
        previous_sign=std::copysign(1.0,c.sweep_degrees);
        double radius=hdist(c.center,route[c.first]),sign=std::copysign(1.0,c.sweep_degrees);
        if(radius+1e-6<p.min_radius_m||radius<=1e-6)fail("curve_radius");
        if(!p.continuous_curves&&std::abs(c.sweep_degrees)>p.max_turn_degrees+1e-6)fail("max_turn_angle");
        double entry=bearing(c.center,route[c.first])+sign*pi/2;
        if(c.first>cursor) {
            auto heading=bearing(route[cursor],route[c.first]);
            if(!aligned(heading,entry)||(exit_heading&&!aligned(*exit_heading,heading)))fail("curve_tangency");
        } else if(!p.continuous_curves||(exit_heading&&!aligned(*exit_heading,entry)))fail("curve_tangency");
        double sweep=0,previous=bearing(c.center,route[c.first]);
        double envelope=radius*(1-std::cos(rad/2))+1e-5;
        for(auto i=c.first+1;i<=c.last;++i) {
            double r=hdist(c.center,route[i]),angle=bearing(c.center,route[i]);
            double delta=sign*std::remainder(angle-previous,2*pi);
            if(std::abs(r-radius)>envelope||delta < -1e-7||delta>rad+1e-6)fail("alignment_arc_shape");
            // A valid chord must stay within the same 1-degree circular envelope.
            auto mid=Vec3{std::midpoint(route[i-1].x,route[i].x),std::midpoint(route[i-1].y,route[i].y),0};
            if(radius-hdist(c.center,mid)>envelope)fail("alignment_arc_shape");
            sweep+=delta;previous=angle;
        }
        if(std::abs(hdist(c.center,route[c.last])-radius)>1e-5||std::abs(sweep-std::abs(c.sweep_degrees)*rad)>1e-6)fail("alignment_arc_shape");
        cursor=c.last;
        exit_heading=bearing(c.center,route[cursor])+sign*pi/2;
        // The next tangent ends at the next arc's start, or at B.
        auto next=&c==&curves.back()?route.size()-1:curves[static_cast<std::size_t>(&c-curves.data())+1].first;
        if(next<cursor||next>=route.size()||(!p.continuous_curves&&next==cursor)||(next>cursor&&!aligned(bearing(route[cursor],route[next]),*exit_heading)))fail("curve_tangency");
    }
    // In continuous mode, an explicit final (or entirely straight) advance is
    // subject to the same minimum as the other tangents, excluding curve length.
    straight(cursor,route.size()-1,!curves.empty()&&!p.continuous_curves);
    if(p.continuous_curves)spacing_check(route,p,v);
}

SearchResult plan_alignment(const Terrain& terrain,Vec3 start,Vec3 finish,const Parameters& p,const UnitCosts& costs,
                            const SearchOptions& resources,const RoadOptions& road,std::stop_token stop) {
    SearchResult result;result.strategy="surface_alignment";
    double step=2*pi/std::ceil(360/road.heading_step_degrees);
    double base=std::max({p.min_straight_m,resources.xy_spacing_m,1.0});
    double cell=std::max(0.01,std::min({resources.xy_spacing_m,p.min_radius_m/4,base/4}));
    result.effective_xy_spacing_m=cell;result.effective_heading_step_degrees=step/rad;
    const auto bounds=terrain.bounds();
    struct Node {Vec3 point;double heading{},g{},turn{},straight{};std::size_t previous{none};};
    std::vector<Node> nodes;
    std::unordered_map<Key,double,Hash> best;
    auto key=[&](Vec3 q,double heading){return Key{static_cast<long long>(std::llround((q.x-bounds[0].x)/cell)),static_cast<long long>(std::llround((q.y-bounds[0].y)/cell)),static_cast<int>(std::llround(wrap(heading)/step))%static_cast<int>(std::llround(2*pi/step))};};
    auto heuristic=[&](Vec3 q){return std::max(hdist(q,finish),(p.max_grade_percent>0?std::abs(q.z-finish.z)*100/p.max_grade_percent:0));};
    using Entry=std::pair<double,std::size_t>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    bool exhausted=false;
    auto profile=[&](Vec3 a,Vec3 b)->std::optional<std::vector<Vec3>> {
        if(stop.stop_requested())return {};
        if(result.edges_checked>=resources.max_edges){exhausted=true;return {};}
        ++result.edges_checked;
        if(b.x<bounds[0].x||b.x>bounds[1].x||b.y<bounds[0].y||b.y>bounds[1].y)return {};
        if(hdist(a,b)<=1e-7)return {};
        auto raw=terrain.segment_profile(a,b,p.max_grade_percent);if(!raw)return {};
        std::vector<Vec3> route;route.reserve(raw->size());
        for(auto q:*raw){if(route.empty()||hdist(route.back(),q)>1e-7)route.push_back(q);else if(std::abs(route.back().z-q.z)>1e-6)return {};}
        if(route.size()<2||std::abs(route.front().z-a.z)>p.surface_tolerance_m)return {};
        route.front()=a;return route;
    };
    auto push=[&](Node n){
        if(nodes.size()>=resources.max_states){exhausted=true;return;}
        auto k=key(n.point,n.heading);auto it=best.find(k);if(it!=best.end()&&it->second<=n.g+1e-8)return;
        best[k]=n.g;auto id=nodes.size();nodes.push_back(n);queue.emplace(n.g+1.5*heuristic(n.point),id);
    };
    auto append=[](std::vector<Vec3>& dst,const std::vector<Vec3>& src){dst.insert(dst.end(),src.begin()+(dst.empty()?0:1),src.end());};
    // The same primitive function validates search actions and reconstructs output.
    auto primitive=[&](Vec3 a,double heading,double turn,double length,std::vector<Vec3>& out,std::vector<AlignmentCurve>& curves)->bool {
        if(std::abs(turn)>1e-8) {
            auto c=center(a,heading,p.min_radius_m,std::copysign(1.0,turn));auto first=out.empty()?0:out.size()-1;
            double count=std::ceil(std::max(std::abs(turn)/rad,p.min_radius_m*std::abs(turn)/p.sample_spacing_m));
            if(count>static_cast<double>(p.max_samples)){exhausted=true;return false;}
            auto n=static_cast<std::size_t>(std::max(1.0,count));auto original=a;
            for(std::size_t i=1;i<=n;++i){auto b=arc_end(original,heading,p.min_radius_m,turn*static_cast<double>(i)/n);auto r=profile(a,b);if(!r)return false;append(out,*r);a=out.back();}
            curves.push_back({first,out.size()-1,c,turn/rad});
        }
        if(length<=1e-8)return true;
        auto b=advance(a,heading+turn,length);auto r=profile(a,b);if(!r)return false;append(out,*r);return true;
    };
    // Direct routes have no interior curve and do not need a between-curves tangent.
    if(auto direct=profile(start,finish)) {
        if(std::abs(direct->back().z-finish.z)<=p.surface_tolerance_m){direct->back()=finish;result.route=std::move(*direct);result.status=SearchStatus::found;result.estimate=estimate_cost(result.route,terrain,p,costs);return result;}
    }
    for(double angle=0;angle<2*pi-1e-8;angle+=step)for(double multiplier:{1.0,2.0,4.0}) {
        if(stop.stop_requested()||exhausted)break;
        auto b=advance(start,angle,base*multiplier);auto r=profile(start,b);if(r)push({r->back(),angle,base*multiplier,0,base*multiplier,none});
    }
    std::vector<double> turns{0};double maximum=p.max_turn_degrees*rad;
    for(double a=step;a<maximum-1e-8;a+=step){turns.push_back(a);turns.push_back(-a);}
    turns.push_back(maximum);turns.push_back(-maximum);
    std::size_t final=none;double final_turn=0,final_length=0;
    auto connector=[&](const Node& n,double turn,double length){
        if(length+1e-6<p.min_straight_m||length<1e-6)return false;
        std::vector<Vec3> route;std::vector<AlignmentCurve> curves;
        if(!primitive(n.point,n.heading,turn,length,route,curves))return false;
        return hdist(route.back(),finish)<1e-5&&std::abs(route.back().z-finish.z)<=p.surface_tolerance_m;
    };
    while(!queue.empty()&&!exhausted) {
        if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
        auto [priority,id]=queue.top();queue.pop();(void)priority;auto n=nodes[id];
        if(n.g>best.at(key(n.point,n.heading))+1e-8)continue;
        if(road.trace){
            auto a=n.previous==none?start:nodes[n.previous].point;
            double heading=n.previous==none?n.heading:nodes[n.previous].heading;
            std::vector<Vec3> branch{a};
            if(std::abs(n.turn)>1e-8)for(int i=1;i<=16;++i){auto q=arc_end(a,heading,p.min_radius_m,n.turn*i/16);q.z=std::lerp(a.z,n.point.z,i/16.0);branch.push_back(q);}
            branch.push_back(n.point);road.trace(branch,false);
        }
        if(result.expanded>=resources.max_expansions){exhausted=true;break;}++result.expanded;
        bool connected=false;
        if(aligned(n.heading,bearing(n.point,finish))&&connector(n,0,hdist(n.point,finish))) {final=id;final_length=hdist(n.point,finish);connected=true;}
        // Analytic circle-to-point tangents allow exact B without snapping the route.
        for(double sign:{-1.0,1.0})if(!connected) {
            auto c=center(n.point,n.heading,p.min_radius_m,sign);double d=hdist(c,finish);if(d<=p.min_radius_m+1e-6)continue;
            double phi=bearing(c,finish),alpha=std::acos(p.min_radius_m/d);
            for(double a:{phi-alpha,phi+alpha}) {
                Vec3 tangent{c.x+p.min_radius_m*std::cos(a),c.y+p.min_radius_m*std::sin(a),0};
                double out=a+sign*pi/2;if(!aligned(out,bearing(tangent,finish)))continue;
                double turn=sign*wrap(sign*(out-n.heading));if(std::abs(turn)>maximum+1e-8||std::abs(turn)<1e-7)continue;
                double length=hdist(tangent,finish);if(connector(n,turn,length)){final=id;final_turn=turn;final_length=length;connected=true;break;}
            }
        }
        if(connected)break;
        for(auto turn:turns) {
            if(stop.stop_requested()||exhausted)break;
            // Compute each arc once, then try several tangent lengths.
            auto a=n.point;std::vector<Vec3> arc;std::vector<AlignmentCurve> curves;
            if(std::abs(turn)>1e-8) {
                if(!primitive(n.point,n.heading,turn,0,arc,curves))continue;
                a=arc[curves.back().last];
            }
            for(double multiplier:{1.0,2.0,4.0}) {
                if(exhausted)break;
                double length=base*multiplier;auto b=advance(a,n.heading+turn,length);
                auto r=profile(a,b);if(!r)continue;
                push({r->back(),wrap(n.heading+turn),n.g+p.min_radius_m*std::abs(turn)+length,turn,length,id});
            }
        }
    }
    if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
    if(final==none){result.status=exhausted?SearchStatus::budget_exceeded:SearchStatus::no_path;return result;}
    // Reconstruction does not consume the exploration budget a second time.
    auto explored=result.edges_checked;exhausted=false;
    std::vector<std::size_t> chain;for(auto i=final;i!=none;i=nodes[i].previous)chain.push_back(i);std::reverse(chain.begin(),chain.end());
    auto first=nodes[chain.front()];result.edges_checked=0;auto initial=profile(start,first.point);
    if(!initial)throw std::runtime_error("Alignment reconstruction failed");result.route=*initial;
    auto reconstruct=[&](Vec3 a,double heading,double turn,double length){
        // All primitives were checked during search. Temporarily reuse an ample budget.
        result.edges_checked=0;
        if(!primitive(a,heading,turn,length,result.route,result.curves))throw std::runtime_error("Alignment reconstruction budget/coverage failure");
    };
    for(std::size_t i=1;i<chain.size();++i){const auto& n=nodes[chain[i]];const auto& prev=nodes[chain[i-1]];reconstruct(prev.point,prev.heading,n.turn,n.straight);}
    const auto& last=nodes[final];reconstruct(last.point,last.heading,final_turn,final_length);result.route.back()=finish;result.edges_checked=explored;
    result.estimate=estimate_cost(result.route,terrain,p,costs,result.curves);result.status=SearchStatus::found;return result;
}
}
