#include "routemine/core.hpp"
#include <cmath>
namespace routemine {
std::vector<double> grades_cpu(std::span<const Vec3> route) {
    (void)measure(route);
    std::vector<double> result(route.size()-1);
    for(std::size_t i=0;i<result.size();++i) {
        const auto d=to_local(route[i+1],route[i]);
        result[i]=std::abs(d.z)/std::hypot(d.x,d.y)*100.0;
    }
    return result;
}
}
