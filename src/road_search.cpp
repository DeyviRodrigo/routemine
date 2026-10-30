#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <numbers>
#include <numeric>
#include <queue>
#include <stdexcept>
#include <unordered_map>

namespace routemine {
namespace {
constexpr auto none=std::numeric_limits<std::size_t>::max();
double horizontal(Vec3 a,Vec3 b){return std::hypot(b.x-a.x,b.y-a.y);}
bool same(Vec3 a,Vec3 b){return horizontal(a,b)<=1e-6&&std::abs(a.z-b.z)<=1e-6;}
double length(std::span<const Vec3> route,std::size_t first,std::size_t last) {
    double sum=0;for(auto i=first;i<last;++i)sum+=std::hypot(horizontal(route[i],route[i+1]),route[i+1].z-route[i].z);return sum;
}
// Every face crossing is retained: endpoints alone cannot certify a surface edge.
std::optional<std::vector<Vec3>> drape(const Terrain& terrain,Vec3 a,Vec3 b,const Parameters& p) {
    auto profile=terrain.segment_profile(a,b,p.max_grade_percent);if(!profile)return {};
    std::vector<Vec3> route;route.reserve(profile->size());
    for(auto q:*profile) {
        if(route.empty()||horizontal(route.back(),q)>1e-7)route.push_back(q);
        else if(std::abs(route.back().z-q.z)>1e-6)return {};
    }
    if(route.size()<2)return {};
    if(std::abs(route.front().z-a.z)>p.surface_tolerance_m||std::abs(route.back().z-b.z)>p.surface_tolerance_m)return {};
    route.front()=a;route.back()=b;
    for(std::size_t i=1;i<route.size();++i) {
        double h=horizontal(route[i-1],route[i]);
        if(h<=1e-8||std::abs(route[i].z-route[i-1].z)>h*p.max_grade_percent/100+1e-8)return {};
    }
    return route;
}
std::vector<std::pair<int,int>> headings(double step) {
    int radius=std::clamp(static_cast<int>(std::ceil(1/std::tan(step*std::numbers::pi/180))),2,64);
    std::vector<std::pair<int,int>> all,selected;
    for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x)
        if((x||y)&&std::gcd(std::abs(x),std::abs(y))==1)all.emplace_back(x,y);
    for(double target=0;target<360-1e-6;target+=step) {
        double angle=target*std::numbers::pi/180,best=10;std::pair<int,int> choice{};
        for(auto offset:all) {
            double delta=std::abs(std::remainder(std::atan2(offset.second,offset.first)-angle,2*std::numbers::pi));
            if(delta<best-1e-12||(std::abs(delta-best)<1e-12&&std::hypot(offset.first,offset.second)<std::hypot(choice.first,choice.second))) {best=delta;choice=offset;}
        }
        selected.push_back(choice);
    }
    for(int y=-1;y<=1;++y)for(int x=-1;x<=1;++x)if(x||y)selected.emplace_back(x,y);
    std::sort(selected.begin(),selected.end());selected.erase(std::unique(selected.begin(),selected.end()),selected.end());return selected;
}
std::vector<double> axis(double lo,double hi,double spacing,double a,double b) {
    std::vector<double> values{lo,hi,a,b};auto count=static_cast<std::size_t>(std::ceil((hi-lo)/spacing));
    for(std::size_t i=1;i<count;++i)values.push_back(lo+i*spacing);
    std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());return values;
}
struct Gateway {std::size_t id;std::vector<Vec3> route;};
struct Patch {std::vector<Gateway> gateways;std::size_t expanded{},edges{};};
struct VertexKey {
    double x,y,z;bool operator==(const VertexKey&)const=default;
};
struct VertexHash {std::size_t operator()(VertexKey p)const{return std::hash<double>{}(p.x)^(std::hash<double>{}(p.y)<<1)^(std::hash<double>{}(p.z)<<2);}};
struct EdgeKey {std::size_t a,b;bool operator==(const EdgeKey&)const=default;};
struct EdgeHash {std::size_t operator()(EdgeKey e)const{return std::hash<std::size_t>{}(e.a)^(std::hash<std::size_t>{}(e.b)<<1);}};
// Exact face-edge states can turn at a crease, where no straight raster edge can cross.
SearchResult mesh_surface_search(const Terrain& terrain,Vec3 start,Vec3 finish,const Parameters& p,
                                 const SearchOptions& resources,const RoadOptions& road,std::stop_token stop) {
    SearchResult result;result.strategy="tin_surface";
    std::vector<Vec3> vertices;std::vector<std::size_t> remap(terrain.vertices().size());
    std::unordered_map<VertexKey,std::size_t,VertexHash> vertex_ids;vertex_ids.reserve(remap.size()/2);
    for(std::size_t i=0;i<remap.size();++i){auto q=terrain.vertices()[i];auto [it,added]=vertex_ids.emplace(VertexKey{q.x,q.y,q.z},vertices.size());if(added)vertices.push_back(q);remap[i]=it->second;}
    std::vector<std::array<std::size_t,3>> triangles,face_edges;
    std::vector<EdgeKey> edges;std::unordered_map<EdgeKey,std::size_t,EdgeHash> edge_ids;
    std::vector<std::vector<std::size_t>> vertex_faces(vertices.size()),edge_faces;
    std::array<std::vector<std::size_t>,2> endpoint_faces;
    for(auto original:terrain.faces()) {
        if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
        std::array<std::size_t,3> f{remap[original[0]],remap[original[1]],remap[original[2]]},fe{};auto face=triangles.size();triangles.push_back(f);
        for(std::size_t j=0;j<3;++j) {
            vertex_faces[f[j]].push_back(face);auto a=f[j],b=f[(j+1)%3];if(a>b)std::swap(a,b);
            auto [it,added]=edge_ids.emplace(EdgeKey{a,b},edges.size());if(added){edges.push_back({a,b});edge_faces.emplace_back();}
            fe[j]=it->second;edge_faces[fe[j]].push_back(face);
        }
        face_edges.push_back(fe);
        auto a=vertices[f[0]],b=to_local(vertices[f[1]],a),c=to_local(vertices[f[2]],a);double det=b.x*c.y-b.y*c.x;
        for(int k=0;k<2;++k){auto q=k?finish:start;double x=q.x-a.x,y=q.y-a.y,u=(x*c.y-y*c.x)/det,v=(b.x*y-b.y*x)/det;
            if(u>=-1e-10&&v>=-1e-10&&u+v<=1+1e-10&&std::abs(q.z-(a.z+u*b.z+v*c.z))<=p.surface_tolerance_m+1e-6)endpoint_faces[k].push_back(face);}
    }
    if(endpoint_faces[0].empty()||endpoint_faces[1].empty())return result;
    std::size_t max_nodes=road.adaptive_resolution?std::max<std::size_t>(resources.max_states,2000000):resources.max_states;
    std::size_t max_edges=road.adaptive_resolution?std::max<std::size_t>(resources.max_edges,10000000):resources.max_edges;
    std::size_t max_expansions=road.adaptive_resolution?std::max<std::size_t>(resources.max_expansions,2000000):resources.max_expansions;
    double edge_sum=0;for(auto e:edges)edge_sum+=horizontal(vertices[e.a],vertices[e.b]);
    if(vertices.size()+2>max_nodes){result.status=SearchStatus::budget_exceeded;return result;}
    auto bounds=terrain.bounds();double z_step=std::clamp(p.max_grade_percent/100*(edge_sum/edges.size())/8,0.02,0.5);
    double z_origin=bounds[0].z;auto level_count=static_cast<std::size_t>(std::ceil((bounds[1].z-z_origin)/z_step))+1;
    struct Node {Vec3 point;std::size_t reference{},previous{none};double distance{std::numeric_limits<double>::infinity()};unsigned char kind{},closed{},valid{};};
    std::vector<Node> nodes;nodes.reserve(std::min<std::size_t>(max_nodes,vertices.size()+200000));
    for(std::size_t i=0;i<vertices.size();++i)nodes.push_back({vertices[i],i});
    auto first=nodes.size();nodes.push_back({start,0,none,0,2});auto last=nodes.size();nodes.push_back({finish,1,none,std::numeric_limits<double>::infinity(),2});
    std::unordered_map<EdgeKey,std::size_t,EdgeHash> samples;
    // A midpoint cannot detect a narrow hole or incompatible overlapping face.
    // Cache exact immutable-TIN edge checks, in both traversal directions.
    std::unordered_map<EdgeKey,unsigned char,EdgeHash> edge_coverage;
    auto incident=[&](std::size_t id)->const std::vector<std::size_t>& {const auto& n=nodes[id];if(n.kind==2)return endpoint_faces[n.reference];if(n.kind==0)return vertex_faces[n.reference];return edge_faces[n.reference];};
    // Prefer a feasible terrain route promptly; no optimality claim is made for this sparse search.
    auto heuristic=[&](Vec3 q){return 2.0*std::max(horizontal(q,finish),(p.max_grade_percent>0?std::abs(q.z-finish.z)*100/p.max_grade_percent:0));};
    using Entry=std::pair<double,std::size_t>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;queue.emplace(heuristic(start),first);
    bool budget=false;
    auto sample=[&](std::size_t edge,std::size_t code,double t)->std::size_t {
        auto e=edges[edge];if(t<=1e-12)return e.a;if(t>=1-1e-12)return e.b;
        auto found=samples.find({edge,code});if(found!=samples.end())return found->second;
        if(nodes.size()>=max_nodes){budget=true;return none;}
        auto a=vertices[e.a],b=vertices[e.b];auto id=nodes.size();
        nodes.push_back({{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)},edge,none,std::numeric_limits<double>::infinity(),1});samples[{edge,code}]=id;return id;
    };
    while(!queue.empty()) {
        if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
        auto [priority,id]=queue.top();queue.pop();if(nodes[id].closed)continue;auto a=nodes[id].point;double cost=nodes[id].distance;
        if(priority>cost+heuristic(a)+1e-8)continue;
        if(id==last){
            std::vector<std::size_t> chain;for(auto cursor=last;cursor!=none;cursor=nodes[cursor].previous)chain.push_back(cursor);std::reverse(chain.begin(),chain.end());
            for(std::size_t i=1;i<chain.size();++i){auto section=drape(terrain,nodes[chain[i-1]].point,nodes[chain[i]].point,p);
                if(!section){result.route.clear();++result.diagnostics.coverage;result.status=SearchStatus::no_path;return result;}
                result.route.insert(result.route.end(),section->begin()+(result.route.empty()?0:1),section->end());
            }
            result.status=SearchStatus::found;result.effective_xy_spacing_m=edge_sum/edges.size();return result;
        }
        if(result.expanded++>=max_expansions){budget=true;break;}nodes[id].closed=1;
        auto relax=[&](std::size_t next){
            if(next==none||next==id||nodes[next].closed)return;auto b=nodes[next].point;double h=horizontal(a,b),candidate=cost+h;
            if(h<=1e-8||candidate>=nodes[next].distance-1e-9||std::abs(b.z-a.z)>h*p.max_grade_percent/100+1e-8)return;
            if(result.edges_checked++>=max_edges){budget=true;return;}
            if(nodes[next].valid==0){auto ground=terrain.elevation(b.x,b.y);nodes[next].valid=ground&&std::abs(*ground-b.z)<=p.surface_tolerance_m+1e-6?1:2;}
            if(nodes[next].valid==2)return;
            const EdgeKey edge{std::min(id,next),std::max(id,next)};auto checked=edge_coverage.find(edge);
            if(checked==edge_coverage.end()){
                const unsigned char state=drape(terrain,a,b,p)?1:terrain.segment_profile(a,b)?2:0;
                checked=edge_coverage.emplace(edge,state).first;
            }
            if(checked->second!=1){if(checked->second==0)++result.diagnostics.coverage;else ++result.diagnostics.grade;return;}
            if(road.trace){std::array<Vec3,2> branch{a,b};road.trace(branch,false);}
            nodes[next].distance=candidate;nodes[next].previous=id;queue.emplace(candidate+heuristic(b),next);
        };
        for(auto face:incident(id)) {
            for(auto vertex:triangles[face])relax(vertex);
            for(int k=0;k<2;++k)if(std::binary_search(endpoint_faces[k].begin(),endpoint_faces[k].end(),face))relax(k?last:first);
            for(auto edge:face_edges[face]) {
                auto e=edges[edge];auto b=vertices[e.a],c=vertices[e.b];double dx=c.x-b.x,dy=c.y-b.y,dz=c.z-b.z,g=p.max_grade_percent/100;
                double x=b.x-a.x,y=b.y-a.y,z=b.z-a.z;
                double aa=dz*dz-g*g*(dx*dx+dy*dy),bb=2*(z*dz-g*g*(x*dx+y*dy)),cc=z*z-g*g*(x*x+y*y);
                std::vector<double> cuts{0,1};double scale=std::max({1.0,std::abs(aa),std::abs(bb),std::abs(cc)});
                auto root=[&](double t){if(t>1e-12&&t<1-1e-12)cuts.push_back(t);};
                if(std::abs(aa)<1e-14*scale){if(std::abs(bb)>1e-14*scale)root(-cc/bb);}
                else {double disc=bb*bb-4*aa*cc;if(disc>=0){double q=-0.5*(bb+std::copysign(std::sqrt(disc),bb));if(q!=0){root(q/aa);root(cc/q);}else root(-bb/(2*aa));}}
                std::sort(cuts.begin(),cuts.end());
                for(std::size_t interval=1;interval<cuts.size();++interval) {
                    double lo=cuts[interval-1],hi=cuts[interval],mid=std::midpoint(lo,hi);
                    if((aa*mid+bb)*mid+cc>1e-12*scale)continue;
                    if(std::abs(dz)>1e-10) {
                        double low=std::min(b.z+lo*dz,b.z+hi*dz),high=std::max(b.z+lo*dz,b.z+hi*dz);
                        long long klo=std::max<long long>(0,static_cast<long long>(std::ceil((low-z_origin)/z_step-1e-9)));
                        long long khi=std::min<long long>(static_cast<long long>(level_count),static_cast<long long>(std::floor((high-z_origin)/z_step+1e-9)));
                        if(klo<=khi) {
                            long long near=static_cast<long long>(std::llround((a.z-z_origin)/z_step)),goal=static_cast<long long>(std::llround((finish.z-z_origin)/z_step));
                            std::vector<long long> codes{klo,khi,(klo+khi)/2,(3*klo+khi)/4,(klo+3*khi)/4};for(long long delta=-2;delta<=2;++delta)codes.push_back(std::clamp(near+delta,klo,khi));codes.push_back(std::clamp(goal,klo,khi));
                            std::sort(codes.begin(),codes.end());codes.erase(std::unique(codes.begin(),codes.end()),codes.end());
                            for(auto k:codes){double t=(z_origin+k*z_step-b.z)/dz;if(t>=lo-1e-10&&t<=hi+1e-10)relax(sample(edge,static_cast<std::size_t>(k),std::clamp(t,0.0,1.0)));}
                        }
                        for(int k=0;k<2;++k){double height=k?finish.z:start.z,t=(height-b.z)/dz;if(t>=lo-1e-10&&t<=hi+1e-10)relax(sample(edge,level_count+1+k,std::clamp(t,0.0,1.0)));}
                    } else for(std::size_t j=1;j<16;++j){double t=static_cast<double>(j)/16;if(t>=lo-1e-10&&t<=hi+1e-10)relax(sample(edge,level_count+3+j,t));}
                    if(budget)break;
                }
                if(budget)break;
            }
            if(budget)break;
        }
        if(budget)break;
    }
    result.status=budget?SearchStatus::budget_exceeded:SearchStatus::no_path;
    return result;
}
// Fine local attachments let an arbitrary endpoint turn before joining a coarser global grid.
Patch endpoint_patch(const Terrain& terrain,Vec3 start,const std::vector<double>& global_x,const std::vector<double>& global_y,
                     const Parameters& p,double global_spacing,std::stop_token stop) {
    Patch result;auto bounds=terrain.bounds();double radius=std::min(80.0,std::max(20.0,global_spacing*2)),step=std::min(0.5,global_spacing/8);
    double xlo=std::max(bounds[0].x,start.x-radius),xhi=std::min(bounds[1].x,start.x+radius);
    double ylo=std::max(bounds[0].y,start.y-radius),yhi=std::min(bounds[1].y,start.y+radius);
    std::vector<double> gx,gy;for(double x:global_x)if(x>=xlo&&x<=xhi)gx.push_back(x);for(double y:global_y)if(y>=ylo&&y<=yhi)gy.push_back(y);
    bool target=false;double minimum=std::min(20.0,std::max(3.0,global_spacing/2));
    for(double x:gx)for(double y:gy)if(std::hypot(x-start.x,y-start.y)>=minimum)target=true;if(!target)return result;
    auto xs=axis(xlo,xhi,step,start.x,start.x),ys=axis(ylo,yhi,step,start.y,start.y);
    xs.insert(xs.end(),gx.begin(),gx.end());ys.insert(ys.end(),gy.begin(),gy.end());
    auto unique=[](auto& values){std::sort(values.begin(),values.end());values.erase(std::unique(values.begin(),values.end()),values.end());};unique(xs);unique(ys);
    auto nx=xs.size(),total=nx*ys.size();if(total>150000)return result;
    auto find=[](const auto& values,double v){return static_cast<std::size_t>(std::lower_bound(values.begin(),values.end(),v)-values.begin());};
    std::map<std::size_t,std::size_t> targets;
    for(double x:gx)for(double y:gy)if(std::hypot(x-start.x,y-start.y)>=minimum)
        targets[find(xs,x)+nx*find(ys,y)]=find(global_x,x)+global_x.size()*find(global_y,y);
    auto first=find(xs,start.x)+nx*find(ys,start.y);
    std::vector<double> z(total,std::numeric_limits<double>::quiet_NaN()),distance(total,std::numeric_limits<double>::infinity());
    std::vector<unsigned char> sampled(total),closed(total);std::vector<std::size_t> previous(total,none);
    std::vector<std::optional<std::array<double,2>>> gradients(total);
    auto coord=[&](std::size_t id)->std::optional<Vec3> {
        if(!sampled[id]){sampled[id]=1;auto height=terrain.elevation(xs[id%nx],ys[id/nx]);if(height){z[id]=*height;gradients[id]=terrain.surface_gradient(xs[id%nx],ys[id/nx]);}}
        if(!std::isfinite(z[id]))return {};return Vec3{xs[id%nx],ys[id/nx],id==first?start.z:z[id]};
    };
    using Entry=std::pair<double,std::size_t>;std::priority_queue<Entry,std::vector<Entry>,std::greater<Entry>> queue;
    distance[first]=0;queue.emplace(0,first);auto offsets=headings(7.5);
    while(!queue.empty()&&result.expanded<20000&&result.edges<150000&&result.gateways.size()<std::min<std::size_t>(32,targets.size())) {
        if(stop.stop_requested())return result;
        auto [cost,id]=queue.top();queue.pop();if(closed[id]||cost>distance[id]+1e-9)continue;closed[id]=1;++result.expanded;
        auto a=coord(id);if(!a)continue;
        if(auto found=targets.find(id);found!=targets.end()) {
            std::vector<Vec3> controls,path;
            for(auto cursor=id;cursor!=none;cursor=previous[cursor])controls.push_back(*coord(cursor));std::reverse(controls.begin(),controls.end());
            for(std::size_t i=1;i<controls.size();++i){auto segment=drape(terrain,controls[i-1],controls[i],p);if(!segment)throw std::runtime_error("Local attachment reconstruction failed");path.insert(path.end(),segment->begin()+(path.empty()?0:1),segment->end());}
            result.gateways.push_back({found->second,std::move(path)});
        }
        auto ix=static_cast<long long>(id%nx),iy=static_cast<long long>(id/nx);
        for(auto [dx,dy]:offsets) {
            auto x=ix+dx,y=iy+dy;if(x<0||y<0||x>=static_cast<long long>(nx)||y>=static_cast<long long>(ys.size()))continue;
            auto next=static_cast<std::size_t>(x)+nx*static_cast<std::size_t>(y);if(closed[next])continue;auto b=coord(next);if(!b)continue;
            double h=horizontal(*a,*b),candidate=cost+h;
            if(candidate>=distance[next]-1e-9||std::abs(a->z-b->z)>h*p.max_grade_percent/100+1e-8)continue;
            auto allowed=[&](const auto& g){return !g||std::abs((*g)[0]*(b->x-a->x)+(*g)[1]*(b->y-a->y))<=h*p.max_grade_percent/100+1e-8;};
            if(!allowed(gradients[id])||!allowed(gradients[next]))continue;
            if(++result.edges>150000||stop.stop_requested())return result;
            if(!drape(terrain,*a,*b,p))continue;distance[next]=candidate;previous[next]=id;queue.emplace(candidate,next);
        }
    }
    return result;
}
// Surface nodes have one elevation, not a product of all terrain elevations.
SearchResult surface_search(const Terrain& terrain,Vec3 start,Vec3 finish,const Parameters& p,
                            const SearchOptions& resources,const RoadOptions& road,double spacing,
                            std::stop_token stop,bool local_attachments=false) {
    SearchResult result;result.strategy="surface_adaptive";result.effective_xy_spacing_m=spacing;
    auto bounds=terrain.bounds();
    double count_x=std::ceil((bounds[1].x-bounds[0].x)/spacing)+4,count_y=std::ceil((bounds[1].y-bounds[0].y)/spacing)+4;
    if(!std::isfinite(count_x)||!std::isfinite(count_y)||count_x*count_y>static_cast<double>(resources.max_states)){result.status=SearchStatus::budget_exceeded;return result;}
    auto xs=axis(bounds[0].x,bounds[1].x,spacing,start.x,finish.x),ys=axis(bounds[0].y,bounds[1].y,spacing,start.y,finish.y);
    if(xs.size()>resources.max_states/ys.size()){result.status=SearchStatus::budget_exceeded;return result;}
    auto nx=xs.size(),total=nx*ys.size();
    auto find=[](const auto& a,double value){return static_cast<std::size_t>(std::lower_bound(a.begin(),a.end(),value)-a.begin());};
    auto first=find(xs,start.x)+nx*find(ys,start.y),last=find(xs,finish.x)+nx*find(ys,finish.y);
    std::vector<double> heights(total,std::numeric_limits<double>::quiet_NaN()),distance(total,std::numeric_limits<double>::infinity());
    std::vector<std::optional<std::array<double,2>>> gradients(total);
    std::vector<unsigned char> sampled(total),closed(total);std::vector<std::size_t> previous(total,none);
    auto coord=[&](std::size_t id)->std::optional<Vec3> {
        if(!sampled[id]) {sampled[id]=1;auto z=terrain.elevation(xs[id%nx],ys[id/nx]);if(z){heights[id]=*z;gradients[id]=terrain.surface_gradient(xs[id%nx],ys[id/nx]);}}
        if(!std::isfinite(heights[id]))return {};
        return Vec3{xs[id%nx],ys[id/nx],id==first?start.z:(id==last?finish.z:heights[id])};
    };
    auto heuristic=[&](Vec3 q){return std::max(horizontal(q,finish),(p.max_grade_percent>0?std::abs(q.z-finish.z)*100/p.max_grade_percent:0));};
    struct Entry {double f,g;std::size_t id;bool operator<(const Entry& o)const{return f>o.f||(f==o.f&&g<o.g);}};
    std::priority_queue<Entry> queue;distance[first]=0;queue.push({heuristic(start),0,first});auto offsets=headings(road.heading_step_degrees);
    std::map<std::size_t,std::vector<Vec3>> start_paths,finish_paths;std::vector<Vec3> selected_suffix;
    std::size_t patch_expanded=0,patch_edges=0;
    if(local_attachments) {
        auto prefix=endpoint_patch(terrain,start,xs,ys,p,spacing,stop),suffix=endpoint_patch(terrain,finish,xs,ys,p,spacing,stop);
        patch_expanded=prefix.expanded+suffix.expanded;patch_edges=prefix.edges+suffix.edges;
        result.expanded=patch_expanded;result.edges_checked=patch_edges;
        for(auto& gateway:prefix.gateways)if(gateway.route.size()>1) {
            double cost=measure(gateway.route).horizontal_m;if(cost<distance[gateway.id]) {
                distance[gateway.id]=cost;previous[gateway.id]=first;queue.push({cost+heuristic(gateway.route.back()),cost,gateway.id});start_paths[gateway.id]=std::move(gateway.route);
            }
        }
        for(auto& gateway:suffix.gateways)if(gateway.route.size()>1)finish_paths[gateway.id]=std::move(gateway.route);
    }
    while(!queue.empty()) {
        if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
        auto current=queue.top();queue.pop();auto id=current.id;
        if(closed[id]||current.g>distance[id]+1e-9)continue;
        if(id==last) {
            std::vector<std::size_t> ids;for(auto cursor=last;cursor!=none;cursor=previous[cursor])ids.push_back(cursor);std::reverse(ids.begin(),ids.end());
            for(std::size_t i=1;i<ids.size();++i) {
                std::optional<std::vector<Vec3>> segment;
                if(ids[i]==last&&!selected_suffix.empty()){segment=selected_suffix;std::reverse(segment->begin(),segment->end());}
                else if(ids[i-1]==first&&start_paths.contains(ids[i]))segment=start_paths.at(ids[i]);
                else segment=drape(terrain,*coord(ids[i-1]),*coord(ids[i]),p);
                if(!segment)throw std::runtime_error("Surface changed while reconstructing route");
                result.route.insert(result.route.end(),segment->begin()+(result.route.empty()?0:1),segment->end());
            }
            result.status=SearchStatus::found;return result;
        }
        if(result.expanded>=resources.max_expansions+patch_expanded){result.status=SearchStatus::budget_exceeded;return result;}
        closed[id]=1;++result.expanded;auto a=coord(id);if(!a)continue;
        if(auto found=finish_paths.find(id);found!=finish_paths.end()) {
            double candidate=current.g+measure(found->second).horizontal_m;
            if(candidate<distance[last]){distance[last]=candidate;previous[last]=id;selected_suffix=found->second;queue.push({candidate,candidate,last});}
        }
        auto ix=static_cast<long long>(id%nx),iy=static_cast<long long>(id/nx);
        auto neighbors=offsets;auto local=gradients[id];if(!local)local=terrain.surface_gradient(a->x,a->y,false);
        if(local) {
            double slope=std::hypot((*local)[0],(*local)[1]);
            if(slope>p.max_grade_percent/100) {
                double bearing=std::atan2((*local)[1],(*local)[0]),alpha=std::acos(p.max_grade_percent/(100*slope));
                int radius_limit=static_cast<int>(p.max_grade_percent>0?std::clamp(std::ceil(2*slope/(p.max_grade_percent/100)),4.0,64.0):64);
                // Supplement the regular stencil near the exact admissible slope directions.
                for(double angle:{bearing+alpha,bearing+std::numbers::pi-alpha,bearing+std::numbers::pi+alpha,bearing-alpha})
                    for(int radius:{2,4,8,16,32,64,radius_limit})for(double margin:{-0.01,0.0,0.01}) {
                        if(radius>radius_limit)continue;
                        int dx=static_cast<int>(std::round(radius*std::cos(angle+margin))),dy=static_cast<int>(std::round(radius*std::sin(angle+margin)));
                        int divisor=std::gcd(std::abs(dx),std::abs(dy));if(divisor)neighbors.emplace_back(dx/divisor,dy/divisor);
                    }
                std::sort(neighbors.begin(),neighbors.end());neighbors.erase(std::unique(neighbors.begin(),neighbors.end()),neighbors.end());
            }
        }
        for(auto [dx,dy]:neighbors) {
            auto x=ix+dx,y=iy+dy;if(x<0||y<0||x>=static_cast<long long>(nx)||y>=static_cast<long long>(ys.size()))continue;
            auto next=static_cast<std::size_t>(x)+nx*static_cast<std::size_t>(y);if(closed[next])continue;
            auto b=coord(next);if(!b)continue;double h=horizontal(*a,*b),candidate=current.g+h;
            if(candidate>=distance[next]-1e-9||std::abs(a->z-b->z)>h*p.max_grade_percent/100+1e-8)continue;
            auto allowed=[&](const auto& gradient){return !gradient||std::abs((*gradient)[0]*(b->x-a->x)+(*gradient)[1]*(b->y-a->y))<=h*p.max_grade_percent/100+1e-8;};
            if(!allowed(gradients[id])||!allowed(gradients[next]))continue;
            if(stop.stop_requested()){result.status=SearchStatus::cancelled;return result;}
            if(result.edges_checked>=resources.max_edges+patch_edges){result.status=SearchStatus::budget_exceeded;return result;}
            ++result.edges_checked;
            auto branch=drape(terrain,*a,*b,p);if(!branch)continue;
            if(road.trace)road.trace(*branch,false);
            distance[next]=candidate;previous[next]=id;start_paths.erase(next);if(next==last)selected_suffix.clear();queue.push({candidate+heuristic(*b),candidate,next});
        }
    }
    return result;
}
}
Parameters road_parameters(const Parameters& p,const RoadOptions& road,bool intervention) {
    Parameters effective=p;effective.mode=intervention?DesignMode::earthwork:(p.continuous_curves?p.mode:DesignMode::surface);
    if(intervention) {
        double angle=std::atan(p.max_grade_percent/100)+road.additional_grade_degrees*std::numbers::pi/180;
        if(angle>std::numbers::pi/4+1e-12)throw std::invalid_argument("Proposed grade angle exceeds supported 45 degrees");
        effective.max_grade_percent=std::tan(angle)*100;
    }
    return effective;
}
void validate_interventions(SearchResult& result,const Terrain& terrain,const Parameters& p,const RoadOptions& road) {
    if(result.interventions.empty()) {
        if(!result.reference_route.empty())throw std::invalid_argument("Reference without intervention spans");return;
    }
    if(result.strategy!="earthwork_shortcuts"||result.reference_route.size()<2)throw std::invalid_argument("Invalid intervention metadata");
    if(result.route.size()<2)throw std::invalid_argument("Missing proposal route");
    auto surface=road_parameters(p,road,false),earthwork=road_parameters(p,road,true);
    auto reference=validate(result.reference_route,terrain,surface);
    if(!reference.checked_constraints_pass())throw std::invalid_argument("Invalid surface reference route");
    if(!same(result.route.front(),result.reference_route.front())||!same(result.route.back(),result.reference_route.back()))
        throw std::invalid_argument("Intervention endpoints differ from reference endpoints");
    std::size_t previous=0,reference_previous=0;
    auto unchanged=[&](std::size_t first,std::size_t last,std::size_t ref_first,std::size_t ref_last) {
        if(last-first!=ref_last-ref_first)throw std::invalid_argument("Unmarked route portion differs from the reference");
        for(std::size_t i=0;i<=last-first;++i)if(!same(result.route[first+i],result.reference_route[ref_first+i]))
            throw std::invalid_argument("Unmarked route portion differs from the reference");
    };
    for(auto& span:result.interventions) {
        if(span.first<previous||span.first>=span.last||span.last>=result.route.size()||span.reference_first<reference_previous||span.reference_first>=span.reference_last||span.reference_last>=result.reference_route.size())
            throw std::invalid_argument("Invalid/overlapping intervention span");
        if(!same(result.route[span.first],result.reference_route[span.reference_first])||!same(result.route[span.last],result.reference_route[span.reference_last]))
            throw std::invalid_argument("Intervention anchors do not match the surface reference");
        unchanged(previous,span.first,reference_previous,span.reference_first);
        span.detour_m=length(result.reference_route,span.reference_first,span.reference_last);span.proposal_m=length(result.route,span.first,span.last);
        if(!(span.detour_m>road.detour_ratio*span.proposal_m+1e-7))throw std::invalid_argument("Intervention does not exceed the strict detour ratio");
        auto validation=validate(std::span(result.route).subspan(span.first,span.last-span.first+1),terrain,earthwork);
        if(!validation.checked_constraints_pass())throw std::invalid_argument("Intervention fails grade/coverage/cut/fill checks");
        if(std::max(validation.max_cut_m,validation.max_fill_m)<=p.surface_tolerance_m)throw std::invalid_argument("Intervention does not require earthwork");
        span.terrain_angle_degrees=0;
        for(auto i=span.first;i<span.last;++i) {
            auto ground=terrain.segment_profile(result.route[i],result.route[i+1]);
            if(!ground)throw std::invalid_argument("Missing terrain for intervention angle");
            for(std::size_t j=1;j<ground->size();++j) {
                double h=horizontal((*ground)[j-1],(*ground)[j]);if(h<=1e-7)continue;
                span.terrain_angle_degrees=std::max(span.terrain_angle_degrees,std::atan2(std::abs((*ground)[j].z-(*ground)[j-1].z),h)*180/std::numbers::pi);
            }
        }
        span.max_cut_m=validation.max_cut_m;span.max_fill_m=validation.max_fill_m;previous=span.last;reference_previous=span.reference_last;
    }
    unchanged(previous,result.route.size()-1,reference_previous,result.reference_route.size()-1);
}
RoadAlternatives plan_road(const Terrain& terrain,Vec3 start,Vec3 finish,const Parameters& p,const UnitCosts& costs,
                          const SearchOptions& resources,const RoadOptions& road,std::stop_token stop,ProgressCallback progress) {
    int last=-1;
    auto report=[&](int value){if(progress&&value>last){last=value;progress(value);}};report(0);
    RoadAlternatives output;if(stop.stop_requested()){output.surface.status=SearchStatus::cancelled;return output;}
    auto done=[&]{
        if(!stop.stop_requested()){
            if(road.trace&&output.surface.status==SearchStatus::found)road.trace(output.surface.route,true);
            if(road.trace&&output.earthwork&&output.earthwork->status==SearchStatus::found)road.trace(output.earthwork->route,true);
            report(100);
        }return std::move(output);
    };
    (void)measure(std::array<Vec3,2>{start,finish});
    if(resources.xy_spacing_m<=0||!std::isfinite(resources.xy_spacing_m)||resources.max_states<16||!resources.max_expansions||!resources.max_edges)
        throw std::invalid_argument("Invalid surface search precision/budgets");
    if(!std::isfinite(road.heading_step_degrees)||road.heading_step_degrees<1||road.heading_step_degrees>45||!std::isfinite(road.detour_ratio)||road.detour_ratio<5||
       !std::isfinite(road.additional_grade_degrees)||road.additional_grade_degrees<0||road.additional_grade_degrees>10||(!road.max_shortcut_checks||!road.max_ramp_candidates))
        throw std::invalid_argument("Invalid road alternative options");
    auto surface=road_parameters(p,road,false);auto bounds=terrain.bounds();RoadOptions directions=road;
    for(auto q:{start,finish}) {
        auto z=terrain.elevation(q.x,q.y);
        if(!z)throw std::invalid_argument("A/B fuera de la superficie disponible o en una zona con cotas ambiguas.");
        if(std::abs(q.z-*z)>surface.surface_tolerance_m)throw std::invalid_argument("La opcion sobre terreno requiere que las cotas A/B coincidan con la TIN. Seleccione los puntos en 3D o corrija Z.");
    }
    // Validate numeric contracts even if the direct segment is not feasible.
    (void)validate(std::array<Vec3,2>{start,finish},terrain,surface);
    report(2);
    if(alignment_enabled(p)) {
        if(p.continuous_curves){
            output.surface=plan_continuous_ramp(terrain,start,finish,surface,costs,resources,road,stop,
                [&](int value){report(2+value*(road.adaptive_resolution?43:93)/100);});
            const auto& initial=output.surface;
            bool length_bound=initial.status==SearchStatus::found&&initial.estimate->validation.metrics.horizontal_m<=horizontal(start,finish)+1e-7&&initial.estimate->total<=initial.estimate->road+1e-8;
            if(road.adaptive_resolution&&!length_bound&&output.surface.status!=SearchStatus::cancelled&&!(road.first_feasible&&output.surface.status==SearchStatus::found)){
                auto limits=resources;limits.xy_spacing_m*=0.75;
                limits.max_states=std::max(resources.max_states,std::min<std::size_t>(2000000,resources.max_states*5));
                limits.max_expansions=std::max(resources.max_expansions,std::min<std::size_t>(2000000,resources.max_expansions*8));
                limits.max_edges=std::max(resources.max_edges,std::min<std::size_t>(10000000,resources.max_edges*8));
                auto attempt=plan_continuous_ramp(terrain,start,finish,surface,costs,limits,road,stop,
                    [&](int value){report(45+value*50/100);});
                auto expanded=attempt.expanded+output.surface.expanded,edges=attempt.edges_checked+output.surface.edges_checked;
                bool better=attempt.status==SearchStatus::found&&(output.surface.status!=SearchStatus::found||attempt.estimate->total<output.surface.estimate->total-1e-8||
                    (std::abs(attempt.estimate->total-output.surface.estimate->total)<=1e-8&&attempt.estimate->validation.metrics.horizontal_m<output.surface.estimate->validation.metrics.horizontal_m));
                const bool limited=output.surface.status==SearchStatus::budget_exceeded||attempt.status==SearchStatus::budget_exceeded;
                if(better||attempt.status==SearchStatus::cancelled||output.surface.status!=SearchStatus::found)output.surface=std::move(attempt);
                if(limited&&output.surface.status!=SearchStatus::found&&output.surface.status!=SearchStatus::cancelled)output.surface.status=SearchStatus::budget_exceeded;
                output.surface.expanded=expanded;output.surface.edges_checked=edges;
                output.messages.push_back("Se refino el paso XY y se ampliaron recursos dentro del limite adaptativo; no se modificaron pendiente, radio, ancho ni profundidades.");
            }
            output.messages.push_back("Rampa con curvas continuas: permite vueltas completas y curvas contiguas del mismo sentido. Exige recta al invertir el giro y conserva radio, pendiente y limites de corte/relleno.");
            output.messages.push_back(output.surface.status!=SearchStatus::found?"Busqueda acotada; no se demuestra imposibilidad ni se garantiza el optimo global.":road.first_feasible?"Primera ruta valida encontrada; la busqueda no garantiza el optimo global.":"Se compara el costo total validado de los candidatos; a igual costo se prefiere menor longitud. Busqueda acotada, sin garantia de optimo global.");
            output.messages.push_back(p.mode==DesignMode::surface?
                "El eje sigue las cotas de la TIN original, sin proyectar corte ni relleno. La plataforma vial completa requiere diseno posterior.":
                "La rasante se comprueba contra la TIN original. Los limites de corte y relleno no se aumentan automaticamente; la estabilidad requiere revision de ingenieria.");
            return done();
        }
        output.surface=plan_alignment(terrain,start,finish,surface,costs,resources,road,stop);
        report(45);
        if(road.adaptive_resolution&&output.surface.status!=SearchStatus::found&&output.surface.status!=SearchStatus::cancelled) {
            auto limits=resources;auto refined=road;limits.xy_spacing_m/=2;refined.heading_step_degrees=std::max(1.0,road.heading_step_degrees/2);
            limits.max_states=std::max(resources.max_states,std::min<std::size_t>(2000000,resources.max_states*2));
            limits.max_expansions=std::max(resources.max_expansions,std::min<std::size_t>(2000000,resources.max_expansions*2));
            limits.max_edges=std::max(resources.max_edges,std::min<std::size_t>(10000000,resources.max_edges*2));
            auto attempt=plan_alignment(terrain,start,finish,surface,costs,limits,refined,stop);
            attempt.expanded+=output.surface.expanded;attempt.edges_checked+=output.surface.edges_checked;output.surface=std::move(attempt);
            output.messages.push_back("Se refinaron XY y direcciones, con ampliacion acotada de recursos; se conservaron radio, recta y giro exigidos.");
        }
        output.messages.push_back("Trazado con tangentes y arcos circulares muestreados: radio, recta minima y angulo total por curva comprobados. Busqueda discreta, sin garantia de optimo global.");
        // A chord shortcut would delete arcs and invalidate their tangency. Until
        // curved earthwork primitives exist, do not offer those unsafe shortcuts.
        if(road.propose_earthwork)output.messages.push_back("Con geometria activa no se proponen atajos rectos de corte: requieren redisenar los empalmes y las curvas.");
        return done();
    }
    auto verified_surface=[&](SearchResult& candidate){
        if(candidate.status!=SearchStatus::found)return false;
        const auto checked=validate(candidate.route,terrain,surface);
        if(checked.checked_constraints_pass())return true;
        for(const auto& code:checked.violations){
            if(code=="terrain_missing_or_ambiguous")++candidate.diagnostics.coverage;
            else if(code=="grade_limit")++candidate.diagnostics.grade;
            else if(code=="surface_separation"||code=="cut_limit"||code=="fill_limit")++candidate.diagnostics.earthwork;
            else ++candidate.diagnostics.geometry;
        }
        candidate.adjustment_report+="Se descartó un candidato sin detener la búsqueda.\n"+validation_failure_message(checked);
        output.messages.push_back("Se descartó un candidato que no superó la validación; se continuaron los intentos disponibles.");
        candidate.route.clear();candidate.curves.clear();candidate.estimate.reset();candidate.status=SearchStatus::no_path;return false;
    };
    if(auto probe=terrain.segment_profile(start,finish)) {
        double maximum=0;
        for(std::size_t i=1;i<probe->size();++i) {
            double h=horizontal((*probe)[i-1],(*probe)[i]);if(h>1e-7)maximum=std::max(maximum,100*std::abs((*probe)[i].z-(*probe)[i-1].z)/h);
        }
        if(maximum>p.max_grade_percent)directions.heading_step_degrees=std::min(road.heading_step_degrees,std::max(1.0,0.5*std::asin(p.max_grade_percent/maximum)*180/std::numbers::pi));
    }
    output.surface.strategy="surface_adaptive";
    if(auto direct=drape(terrain,start,finish,surface)) {
        output.surface.route=std::move(*direct);output.surface.status=SearchStatus::found;output.surface.effective_xy_spacing_m=resources.xy_spacing_m;
    } else {
        double spacing=resources.xy_spacing_m;
        auto cells=[&](double s){return (std::ceil((bounds[1].x-bounds[0].x)/s)+4)*(std::ceil((bounds[1].y-bounds[0].y)/s)+4);};
        if(road.adaptive_resolution)while(cells(spacing)>static_cast<double>(resources.max_states))spacing*=1.25;
        if(!std::isfinite(spacing))throw std::invalid_argument("Terrain extent exceeds finite search domain");
        auto requested=spacing;
        std::size_t total_expanded=0,total_edges=0;
        int attempt_index=0;
        for(double factor:std::array<double,5>{4,2,1,0.5,0.25}) {
            if(!road.adaptive_resolution&&factor!=1)continue;
            report(5+attempt_index++*12);
            auto limits=resources;
            if(factor<1) {
                double multiplier=1/(factor*factor);
                limits.max_states=std::max(resources.max_states,std::min<std::size_t>(2000000,static_cast<std::size_t>(resources.max_states*multiplier)));
                limits.max_expansions=std::max(resources.max_expansions,limits.max_states);
                limits.max_edges=std::max(resources.max_edges,std::min<std::size_t>(10000000,static_cast<std::size_t>(resources.max_edges*multiplier)));
            }
            auto attempt_directions=directions;if(factor>1)attempt_directions.heading_step_degrees=road.heading_step_degrees;
            auto result=surface_search(terrain,start,finish,surface,limits,attempt_directions,spacing*factor,stop);
            (void)verified_surface(result);
            if(factor==4&&road.adaptive_resolution&&result.status!=SearchStatus::found&&result.status!=SearchStatus::cancelled) {
                auto mesh=mesh_surface_search(terrain,start,finish,surface,resources,road,stop);
                (void)verified_surface(mesh);
                if(mesh.status==SearchStatus::found||mesh.status==SearchStatus::cancelled){mesh.expanded+=result.expanded;mesh.edges_checked+=result.edges_checked;output.surface=std::move(mesh);total_expanded+=output.surface.expanded;total_edges+=output.surface.edges_checked;break;}
                total_expanded+=mesh.expanded;total_edges+=mesh.edges_checked;
            }
            if(result.status!=SearchStatus::found&&result.status!=SearchStatus::cancelled&&road.adaptive_resolution) {
                auto attached=surface_search(terrain,start,finish,surface,limits,attempt_directions,spacing*factor,stop,true);
                (void)verified_surface(attached);
                attached.expanded+=result.expanded;attached.edges_checked+=result.edges_checked;result=std::move(attached);
            }
            result.effective_heading_step_degrees=attempt_directions.heading_step_degrees;
            total_expanded+=result.expanded;total_edges+=result.edges_checked;output.surface=std::move(result);
            if(output.surface.status==SearchStatus::found||output.surface.status==SearchStatus::cancelled)break;
        }
        output.surface.expanded=total_expanded;output.surface.edges_checked=total_edges;
        if(requested!=resources.xy_spacing_m)output.messages.push_back("Resolucion XY adaptada al presupuesto; consulte el paso efectivo del resultado.");
        if(output.surface.effective_xy_spacing_m<requested)output.messages.push_back("Se refino XY y se ampliaron presupuestos iniciales dentro del limite adaptativo (hasta 2 millones de nodos y 10 millones de aristas por intento).");
    }
    if(output.surface.status!=SearchStatus::found||!verified_surface(output.surface))return done();
    if(output.surface.effective_heading_step_degrees==0&&output.surface.strategy!="tin_surface")output.surface.effective_heading_step_degrees=directions.heading_step_degrees;
    output.surface.estimate=estimate_cost(output.surface.route,terrain,surface,costs);
    report(70);
    if(!road.propose_earthwork||stop.stop_requested())return done();
    const auto& reference=output.surface.route;auto earthwork=road_parameters(p,road,true);
    std::vector<double> station(reference.size());for(std::size_t i=1;i<reference.size();++i)station[i]=station[i-1]+std::hypot(horizontal(reference[i-1],reference[i]),reference[i].z-reference[i-1].z);
    // At most 1000 anchors; all saved anchors remain exact vertices of the first route.
    double anchor_spacing=std::max({resources.xy_spacing_m,station.back()/1000,1.0});
    std::vector<std::size_t> anchors{0};for(std::size_t i=1;i<reference.size();++i)if(station[i]-station[anchors.back()]>=anchor_spacing)anchors.push_back(i);
    if(anchors.back()!=reference.size()-1)anchors.push_back(reference.size()-1);
    struct Candidate {std::size_t first,last;double saving;};std::vector<Candidate> candidates;
    for(std::size_t a=0;a+1<anchors.size();++a)for(std::size_t b=a+1;b<anchors.size();++b) {
        auto i=anchors[a],j=anchors[b];double h=horizontal(reference[i],reference[j]),chord=std::hypot(h,reference[j].z-reference[i].z),detour=station[j]-station[i];
        if(h<1||detour<=road.detour_ratio*chord+1e-7||std::abs(reference[j].z-reference[i].z)>h*earthwork.max_grade_percent/100+1e-8)continue;
        candidates.push_back({i,j,detour-chord});
    }
    std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.saving>b.saving;});
    std::vector<Candidate> accepted;std::size_t checked=0;
    for(auto candidate:candidates) {
        if(stop.stop_requested())return output;
        report(75+static_cast<int>(20.0*checked/std::max<std::size_t>(1,std::min(candidates.size(),road.max_shortcut_checks))));
        bool overlap=false;for(auto other:accepted)if(candidate.first<other.last&&candidate.last>other.first){overlap=true;break;}if(overlap)continue;
        if(checked++>=road.max_shortcut_checks){output.messages.push_back("Se alcanzo el presupuesto de comparacion de atajos; la revision no es exhaustiva.");break;}
        auto validation=validate(std::array<Vec3,2>{reference[candidate.first],reference[candidate.last]},terrain,earthwork);
        if(validation.checked_constraints_pass()&&std::max(validation.max_cut_m,validation.max_fill_m)>p.surface_tolerance_m)accepted.push_back(candidate);
    }
    if(accepted.empty()){output.messages.push_back("Sin atajos verificados: se exige el umbral de distancias y respetar angulo, cobertura y limites de corte/relleno.");return done();}
    std::sort(accepted.begin(),accepted.end(),[](auto a,auto b){return a.first<b.first;});
    SearchResult proposal;proposal.strategy="earthwork_shortcuts";proposal.status=SearchStatus::found;proposal.reference_route=reference;proposal.effective_xy_spacing_m=output.surface.effective_xy_spacing_m;proposal.effective_heading_step_degrees=output.surface.effective_heading_step_degrees;
    std::size_t cursor=0;
    for(auto candidate:accepted) {
        proposal.route.insert(proposal.route.end(),reference.begin()+cursor,reference.begin()+candidate.first+1);
        auto first=proposal.route.size()-1;proposal.route.push_back(reference[candidate.last]);
        proposal.interventions.push_back({first,first+1,candidate.first,candidate.last});cursor=candidate.last+1;
    }
    proposal.route.insert(proposal.route.end(),reference.begin()+cursor,reference.end());
    validate_interventions(proposal,terrain,p,road);proposal.estimate=estimate_cost(proposal.route,terrain,earthwork,costs);output.earthwork=std::move(proposal);return done();
}
}
