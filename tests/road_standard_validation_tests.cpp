#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
#include <string>
using namespace routemine;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
bool has(const Validation& value,const char* code){return std::find(value.violations.begin(),value.violations.end(),code)!=value.violations.end();}
bool pending(const Validation& value,const char* code){return std::find(value.not_evaluated.begin(),value.not_evaluated.end(),code)!=value.not_evaluated.end();}
Terrain terrain(){return Terrain({{-3000,-3000,0},{3000,-3000,0},{-3000,3000,0},{3000,3000,0}},{{0,1,2},{1,3,2}});}
Parameters parameters(){Parameters p;p.mode=DesignMode::earthwork;p.max_grade_percent=12;p.max_cut_m=1000;p.max_fill_m=1000;p.road_width_m=2;p.apply_dg_grade_guidance=true;return p;}
struct Shape {
    SearchResult result;double heading{},grade{};
    explicit Shape(double slope=0):grade(slope){result.route={{0,0,0}};}
    void straight(double length){if(length<=0)return;const auto a=result.route.back();
        // Extra vertices verify that the threshold applies to the full block.
        for(int i=1;i<=4;++i){const double t=static_cast<double>(i)/4;result.route.push_back({a.x+length*t*std::cos(heading),a.y+length*t*std::sin(heading),a.z+length*t*grade});}}
    void arc(double radius,double degrees){const auto a=result.route.back();const double sign=std::copysign(1.,degrees),rad=std::numbers::pi/180;
        Vec3 center{a.x-sign*radius*std::sin(heading),a.y+sign*radius*std::cos(heading),0};const double start=std::atan2(a.y-center.y,a.x-center.x);
        const auto first=result.route.size()-1;const auto count=static_cast<std::size_t>(std::ceil(std::abs(degrees)));double z=a.z;
        for(std::size_t i=1;i<=count;++i){const double angle=start+degrees*rad*static_cast<double>(i)/count;Vec3 q{center.x+radius*std::cos(angle),center.y+radius*std::sin(angle),0};
            const auto previous=result.route.back();z+=grade*std::hypot(q.x-previous.x,q.y-previous.y);q.z=z;result.route.push_back(q);}
        result.curves.push_back({first,result.route.size()-1,center,degrees});heading+=degrees*rad;
    }
};
Validation verify(const Shape& shape,const Parameters& p){return validate(shape.result.route,terrain(),p,shape.result.curves);}
}
int main(int argc,char** argv){try{
    check(argc==2,"expected a test case");const std::string name=argv[1];auto p=parameters();
    if(name=="tight_curve"){
        p.continuous_curves=true;p.min_radius_m=20;
        Shape tight(.09);tight.arc(30,90);auto value=verify(tight,p);
        check(has(value,"dg_tight_curve_grade"),"nine-percent tight curve bypassed conservative DG guidance");
        check(!value.diagnostics.empty()&&value.diagnostics.front().find("Tramo ")!=std::string::npos&&value.diagnostics.front().find("8 %")!=std::string::npos,"tight-curve issue lacks localized Spanish diagnostic");
        Shape boundary(.08);boundary.arc(30,90);check(verify(boundary,p).checked_constraints_pass(),"eight-percent tight curve was rejected");
        Shape wide(.09);wide.arc(50,90);check(verify(wide,p).checked_constraints_pass(),"50-m curve was incorrectly classified as below50m");
        Shape descending(-.09);descending.arc(30,-90);check(has(verify(descending,p),"dg_tight_curve_grade"),"descending tight-curve grade escaped the limit");
    }else if(name=="long_grade"){
        std::vector<Vec3> exact{{0,0,0},{90,0,9.9},{180,0,19.8}};
        check(validate(exact,terrain(),p).checked_constraints_pass(),"180-m steep section was rejected");
        exact.back()={180.01,0,19.8011};auto value=validate(exact,terrain(),p);check(has(value,"dg_long_steep_grade"),"steep run length was checked per edge instead of cumulatively");
        check(value.diagnostics.front().find("Tramo 1")!=std::string::npos&&value.diagnostics.front().find("180 m")!=std::string::npos,"steep-run diagnosis lost the run's starting station");
        std::vector<Vec3> reset{{0,0,0},{100,0,11},{110,0,11},{210,0,22}};check(validate(reset,terrain(),p).checked_constraints_pass(),"lower-grade section did not reset the steep run");
        std::vector<Vec3> ten{{0,0,0},{500,0,50}};check(validate(ten,terrain(),p).checked_constraints_pass(),"exactly ten-percent grade acquired the over-ten-percent length restriction");
        std::vector<Vec3> descent{{0,0,0},{181,0,-19.91}};check(has(validate(descent,terrain(),p),"dg_long_steep_grade"),"long steep descent bypassed DG run check");
        check(pending(value,"dg_average_grade_over_2000m")&&pending(value,"dg_rest_sections_after_3000m"),"longitudinal studies were silently certified");
    }else if(name=="disabled"){
        p.apply_dg_grade_guidance=false;p.continuous_curves=true;p.min_radius_m=20;
        Shape tight(.09);tight.arc(30,90);auto value=verify(tight,p);check(value.checked_constraints_pass(),"DG opt-out changed existing curve validation");
        std::vector<Vec3> long_route{{0,0,0},{300,0,33}};value=validate(long_route,terrain(),p);check(value.checked_constraints_pass(),"DG opt-out changed existing grade validation");
        check(!pending(value,"dg_average_grade_over_2000m"),"disabled DG guidance inserted enabled-only pending studies");
        p.apply_dg_grade_guidance=true;p.max_grade_percent=8;check(has(verify(tight,p),"grade_limit"),"DG flag weakened the requested maximum grade");
    }else if(name=="straight_pairs"){
        p.continuous_curves=true;p.min_radius_m=30;p.min_straight_m=42;p.min_straight_same_direction_m=84;
        auto pair=[&](double middle,double second){Shape result;result.straight(50);result.arc(40,90);result.straight(middle);result.arc(40,second);result.straight(50);return result;};
        auto value=verify(pair(60,90),p);check(has(value,"straight_same_direction"),"same-direction pair ignored the larger O minimum");
        check(value.diagnostics.front().find("84.000000")!=std::string::npos&&value.diagnostics.front().find("mismo sentido")!=std::string::npos,"same-direction diagnostic showed the wrong minimum");
        check(verify(pair(84,90),p).checked_constraints_pass(),"same-direction tangent at84m was rejected");
        check(verify(pair(60,-90),p).checked_constraints_pass(),"opposite-direction pair inherited the O minimum");
        check(verify(pair(0,90),p).checked_constraints_pass(),"contiguous same-direction arcs acquired a positive tangent requirement");
        check(has(verify(pair(0,-90),p),"straight_before_turn"),"contiguous opposite-direction arcs bypassed S minimum");
        p.min_straight_same_direction_m=0;check(verify(pair(60,90),p).checked_constraints_pass(),"default zero O minimum changed existing behavior");
    }else throw std::invalid_argument("unknown test case");
    std::cout<<name<<": PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
