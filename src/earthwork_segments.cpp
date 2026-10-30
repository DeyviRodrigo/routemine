#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
namespace routemine {
std::vector<EarthworkSegment> earthwork_segments(const Terrain& terrain,std::span<const Vec3> route,double tolerance){
    if(!std::isfinite(tolerance)||tolerance<0)throw std::invalid_argument("Invalid earthwork tolerance");
    (void)measure(route);std::vector<EarthworkSegment> result;
    for(std::size_t i=1;i<route.size();++i){auto a=route[i-1],b=route[i];double length=std::hypot(b.x-a.x,b.y-a.y);
        auto profile=terrain.segment_profile(a,b);if(!profile)throw std::invalid_argument("Missing terrain for earthwork colors");
        auto station=[&](Vec3 q){return std::clamp(std::hypot(q.x-a.x,q.y-a.y)/length,0.0,1.0);};
        auto design=[&](double t){return Vec3{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};};
        for(std::size_t j=1;j<profile->size();++j){double t0=station((*profile)[j-1]),t1=station((*profile)[j]);
            double d0=std::lerp(a.z,b.z,t0)-(*profile)[j-1].z,d1=std::lerp(a.z,b.z,t1)-(*profile)[j].z;
            std::vector<double> cuts{0,1};if(std::abs(d1-d0)>1e-15)for(double threshold:{-tolerance,tolerance}){double t=(threshold-d0)/(d1-d0);if(t>1e-12&&t<1-1e-12)cuts.push_back(t);}
            std::sort(cuts.begin(),cuts.end());
            for(std::size_t k=1;k<cuts.size();++k){double mid=std::lerp(d0,d1,(cuts[k-1]+cuts[k])/2);
                auto kind=mid < -tolerance?EarthworkKind::cut:mid > tolerance?EarthworkKind::fill:EarthworkKind::surface;
                auto first=design(std::lerp(t0,t1,cuts[k-1])),last=design(std::lerp(t0,t1,cuts[k]));
                if(std::hypot(last.x-first.x,last.y-first.y)>1e-8)result.push_back({first,last,kind,i-1});
            }
        }
    }return result;
}
}
