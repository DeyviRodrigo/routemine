#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace routemine {
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
}
std::optional<double> Terrain::elevation(double x,double y) const {
    if(!std::isfinite(x)||!std::isfinite(y)) return std::nullopt;
    std::optional<double> found;
    for(auto face:faces_) {
        const auto a=vertices_[face[0]], b=to_local(vertices_[face[1]],a), c=to_local(vertices_[face[2]],a);
        const double px=x-a.x, py=y-a.y, det=b.x*c.y-b.y*c.x;
        const double u=(px*c.y-py*c.x)/det, v=(b.x*py-b.y*px)/det;
        if(u < -1e-12 || v < -1e-12 || u+v > 1.0+1e-12) continue;
        const double z=a.z+u*b.z+v*c.z;
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
std::optional<std::vector<Vec3>> Terrain::segment_profile(Vec3 a,Vec3 b) const {
    if(!finite(a)||!finite(b)||std::hypot(b.x-a.x,b.y-a.y)<=1e-8)return std::nullopt;
    std::vector<double> breaks{0,1};
    for(auto face:faces_){
        auto o=vertices_[face[0]],u=to_local(vertices_[face[1]],o),v=to_local(vertices_[face[2]],o);
        double det=u.x*v.y-u.y*v.x;
        auto weights=[&](Vec3 p){double x=p.x-o.x,y=p.y-o.y;
            double s=(x*v.y-y*v.x)/det,t=(u.x*y-u.y*x)/det;return std::array<double,3>{1-s-t,s,t};};
        auto wa=weights(a),wb=weights(b);double lo=0,hi=1;bool hit=true;
        for(std::size_t j=0;j<3;++j){double d=wb[j]-wa[j];
            if(std::abs(d)<1e-15){if(wa[j]<-1e-12)hit=false;}
            else {double cross=-wa[j]/d;if(d>0)lo=std::max(lo,cross);else hi=std::min(hi,cross);}
        }
        if(hit && lo<=hi && lo<=1 && hi>=0){breaks.push_back(std::clamp(lo,0.0,1.0));breaks.push_back(std::clamp(hi,0.0,1.0));}
    }
    std::sort(breaks.begin(),breaks.end());
    breaks.erase(std::unique(breaks.begin(),breaks.end()),breaks.end());
    std::vector<Vec3> result;
    for(std::size_t i=0;i<breaks.size();++i){
        auto sample=[&](double t)->std::optional<Vec3>{
            double x=std::lerp(a.x,b.x,t),y=std::lerp(a.y,b.y,t);auto z=elevation(x,y);
            if(!z)return std::nullopt;return Vec3{x,y,*z};};
        auto p=sample(breaks[i]);if(!p)return std::nullopt;
        result.push_back(*p);
        if(i && !sample(std::midpoint(breaks[i-1],breaks[i])))return std::nullopt;
    }
    return result;
}
}
