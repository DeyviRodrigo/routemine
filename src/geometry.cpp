#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace routemine {
namespace {
double coordinate_ulp(double value){return std::abs(std::nextafter(value,std::numeric_limits<double>::infinity())-value);}
struct FaceWeights {double first,second,third;};
// Barycentric errors at UTM coordinates are controlled in physical XY units,
// not by expanding every triangle by a fixed barycentric fraction. This only
// accepts positions indistinguishable from an edge at coordinate roundoff.
std::optional<FaceWeights> face_weights(Vec3 point,Vec3 a,Vec3 b,Vec3 c){
    auto u=to_local(b,a),v=to_local(c,a);double det=u.x*v.y-u.y*v.x;
    double px=point.x-a.x,py=point.y-a.y;
    double second=(px*v.y-py*v.x)/det,third=(u.x*py-u.y*px)/det,first=1-second-third;
    double side0=std::hypot(c.x-b.x,c.y-b.y),side1=std::hypot(v.x,v.y),side2=std::hypot(u.x,u.y);
    double longest=std::max({side0,side1,side2});
    double tol=4*std::max({coordinate_ulp(point.x),coordinate_ulp(point.y),coordinate_ulp(a.x),coordinate_ulp(a.y),coordinate_ulp(b.x),coordinate_ulp(b.y),coordinate_ulp(c.x),coordinate_ulp(c.y)})+
        16*std::numeric_limits<double>::epsilon()*longest;
    double area=std::abs(det);
    if(first*area < -tol*side0||second*area < -tol*side1||third*area < -tol*side2)return {};
    // Project only roundoff-sized negative weights back onto the original edge.
    first=std::max(0.,first);second=std::max(0.,second);third=std::max(0.,third);
    double sum=first+second+third;return FaceWeights{first/sum,second/sum,third/sum};
}
}

bool finite(Vec3 p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }
Vec3 to_local(Vec3 p, Vec3 o) { return {p.x-o.x, p.y-o.y, p.z-o.z}; }
Vec3 to_world(Vec3 p, Vec3 o) { return {p.x+o.x, p.y+o.y, p.z+o.z}; }
Metrics measure(std::span<const Vec3> route) {
    if (route.size()<2) throw std::invalid_argument("Route needs at least two points");
    Metrics m;
    for (const auto p:route) if (!finite(p)) throw std::invalid_argument("Non-finite coordinate");
    for (std::size_t i=1;i<route.size();++i) {
        const auto d=to_local(route[i],route[i-1]);
        const double h=std::hypot(d.x,d.y), spatial=std::hypot(h,d.z);
        if (!(h>1e-8) || !std::isfinite(spatial)) throw std::invalid_argument("Degenerate or overflowing segment");
        const double g=std::abs(d.z)/h*100.0;
        m.horizontal_m+=h; m.spatial_m+=spatial;
        m.max_grade_percent=std::max(m.max_grade_percent,g);
        if (!std::isfinite(m.horizontal_m) || !std::isfinite(m.spatial_m) || !std::isfinite(g))
            throw std::invalid_argument("Overflowing route metrics");
    }
    return m;
}
Terrain::Terrain(std::vector<Vec3> vertices, std::vector<std::array<std::size_t,3>> faces)
    :vertices_(std::move(vertices)),faces_(std::move(faces)) {
    if (faces_.empty()) throw std::invalid_argument("Terrain has no faces");
    for (auto p:vertices_) if (!finite(p)) throw std::invalid_argument("Non-finite terrain coordinate");
    for (auto face:faces_) {
        for (auto id:face) if(id>=vertices_.size()) throw std::invalid_argument("Invalid face index");
        const auto a=vertices_[face[0]], b=to_local(vertices_[face[1]],a), c=to_local(vertices_[face[2]],a);
        const double det=b.x*c.y-b.y*c.x;
        if (!std::isfinite(det) || std::abs(det)<=1e-12) throw std::invalid_argument("Face is degenerate in XY");
    }
    face_order_.resize(faces_.size());std::iota(face_order_.begin(),face_order_.end(),0);
    index_.reserve(2*faces_.size());build_index(0,faces_.size());
}
std::size_t Terrain::build_index(std::size_t begin,std::size_t end) {
    auto first=vertices_[faces_[face_order_[begin]][0]];Node node{first,first,begin,end,0,0};
    for(auto i=begin;i<end;++i)for(auto id:faces_[face_order_[i]]) {
        auto p=vertices_[id];node.lo.x=std::min(node.lo.x,p.x);node.lo.y=std::min(node.lo.y,p.y);
        node.hi.x=std::max(node.hi.x,p.x);node.hi.y=std::max(node.hi.y,p.y);
    }
    auto id=index_.size();index_.push_back(node);
    if(end-begin>12) {
        bool x=node.hi.x-node.lo.x>=node.hi.y-node.lo.y;auto mid=begin+(end-begin)/2;
        auto center=[&](std::size_t face) {
            const auto f=faces_[face];auto a=vertices_[f[0]],b=vertices_[f[1]],c=vertices_[f[2]];
            return x?(a.x/3+b.x/3+c.x/3):(a.y/3+b.y/3+c.y/3);
        };
        std::nth_element(face_order_.begin()+begin,face_order_.begin()+mid,face_order_.begin()+end,
                         [&](auto a,auto b){return center(a)<center(b);});
        auto left=build_index(begin,mid),right=build_index(mid,end);
        index_[id].left=left;index_[id].right=right;
    }
    return id;
}
std::vector<std::size_t> Terrain::candidates(Vec3 lo,Vec3 hi) const {
    std::vector<std::size_t> result,stack{0};
    while(!stack.empty()) {
        auto id=stack.back();stack.pop_back();const auto& node=index_[id];
        // Match barycentric edge tolerance, including triangles far from the local origin.
        double pad_x=std::max(1e-11*std::max(1.0,node.hi.x-node.lo.x),4*std::max({coordinate_ulp(node.lo.x),coordinate_ulp(node.hi.x),coordinate_ulp(lo.x),coordinate_ulp(hi.x)}));
        double pad_y=std::max(1e-11*std::max(1.0,node.hi.y-node.lo.y),4*std::max({coordinate_ulp(node.lo.y),coordinate_ulp(node.hi.y),coordinate_ulp(lo.y),coordinate_ulp(hi.y)}));
        if(hi.x<node.lo.x-pad_x||lo.x>node.hi.x+pad_x||hi.y<node.lo.y-pad_y||lo.y>node.hi.y+pad_y)continue;
        if(node.left==0)result.insert(result.end(),face_order_.begin()+node.begin,face_order_.begin()+node.end);
        else {stack.push_back(node.left);stack.push_back(node.right);}
    }
    return result;
}
std::optional<double> Terrain::elevation(double x,double y) const {
    if(!std::isfinite(x)||!std::isfinite(y)) return std::nullopt;
    std::optional<double> found;
    for(auto id:candidates({x,y,0},{x,y,0})) {
        auto face=faces_[id];
        const auto a=vertices_[face[0]],b=vertices_[face[1]],c=vertices_[face[2]];
        auto weights=face_weights({x,y,0},a,b,c);if(!weights)continue;
        const double z=a.z+weights->second*(b.z-a.z)+weights->third*(c.z-a.z);
        if(!std::isfinite(z)) return std::nullopt;
        if(found && std::abs(*found-z)>1e-6) return std::nullopt;
        found=z;
    }
    return found;
}
std::array<Vec3,2> Terrain::bounds() const {
    auto lo=vertices_.front(),hi=lo;
    for(auto p:vertices_){lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);
        hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}
    return {lo,hi};
}
std::optional<std::array<double,2>> Terrain::surface_gradient(double x,double y,bool interior_only) const {
    for(auto id:candidates({x,y,0},{x,y,0})) {
        auto face=faces_[id];auto a=vertices_[face[0]],b=to_local(vertices_[face[1]],a),c=to_local(vertices_[face[2]],a);
        double det=b.x*c.y-b.y*c.x,px=x-a.x,py=y-a.y;
        double u=(px*c.y-py*c.x)/det,v=(b.x*py-b.y*px)/det;
        if((interior_only&&u>1e-8&&v>1e-8&&u+v<1-1e-8)||(!interior_only&&face_weights({x,y,0},a,vertices_[face[1]],vertices_[face[2]])))
            return std::array<double,2>{(b.z*c.y-c.z*b.y)/det,(b.x*c.z-c.x*b.z)/det};
    }
    return {};
}
std::optional<std::vector<Vec3>> Terrain::segment_profile(Vec3 a,Vec3 b,double max_surface_grade_percent) const {
    if(!finite(a)||!finite(b)||std::hypot(b.x-a.x,b.y-a.y)<=1e-8)return std::nullopt;
    std::vector<double> breaks{0,1};
    for(auto id:candidates({std::min(a.x,b.x),std::min(a.y,b.y),0},{std::max(a.x,b.x),std::max(a.y,b.y),0})){
        auto face=faces_[id];
        auto o=vertices_[face[0]],u=to_local(vertices_[face[1]],o),v=to_local(vertices_[face[2]],o);
        double det=u.x*v.y-u.y*v.x;
        auto weights=[&](Vec3 p){double x=p.x-o.x,y=p.y-o.y;
            double s=(x*v.y-y*v.x)/det,t=(u.x*y-u.y*x)/det;return std::array<double,3>{1-s-t,s,t};};
        auto wa=weights(a),wb=weights(b);double lo=0,hi=1;bool hit=true;
        for(std::size_t j=0;j<3;++j){double d=wb[j]-wa[j];
            if(std::abs(d)<1e-15){if(wa[j]<-1e-12)hit=false;}
            else {double cross=-wa[j]/d;if(d>0)lo=std::max(lo,cross);else hi=std::min(hi,cross);}
        }
        if(hit && lo<=hi && lo<=1 && hi>=0){
            lo=std::clamp(lo,0.0,1.0);hi=std::clamp(hi,0.0,1.0);
            if(hi-lo>1e-12&&std::isfinite(max_surface_grade_percent)) {
                double rise=(wb[1]-wa[1])*u.z+(wb[2]-wa[2])*v.z;
                if(std::abs(rise)*100>std::hypot(b.x-a.x,b.y-a.y)*(max_surface_grade_percent+1e-8))return std::nullopt;
            }
            breaks.push_back(lo);breaks.push_back(hi);
        }
    }
    std::sort(breaks.begin(),breaks.end());
    breaks.erase(std::unique(breaks.begin(),breaks.end()),breaks.end());
    std::vector<Vec3> result;
    for(std::size_t i=0;i<breaks.size();++i){
        auto sample=[&](double t)->std::optional<Vec3>{
            double x=std::lerp(a.x,b.x,t),y=std::lerp(a.y,b.y,t);auto z=elevation(x,y);
            if(!z)return std::nullopt;return Vec3{x,y,*z};};
        auto p=sample(breaks[i]);if(!p)return std::nullopt;
        // Adjacent faces can compute the same boundary at slightly different
        // parameters. Check every original interval for coverage before merging
        // redundant samples, so this cannot bridge a hole in the TIN.
        if(i && !sample(std::midpoint(breaks[i-1],breaks[i])))return std::nullopt;
        if(!result.empty()&&std::hypot(p->x-result.back().x,p->y-result.back().y)<=1e-8){
            // A vertical discontinuity is not a redundant boundary sample.
            if(std::abs(p->z-result.back().z)>1e-8)return std::nullopt;
            if(i+1==breaks.size())result.back()=*p; // Preserve the exact endpoint.
        }else result.push_back(*p);
    }
    return result;
}
}
