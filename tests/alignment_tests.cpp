#include "routemine/config.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace routemine;
void check(bool ok,const char* message="assertion failed"){if(!ok)throw std::runtime_error(message);}
bool has(const Validation& v,const char* key){return std::find(v.violations.begin(),v.violations.end(),key)!=v.violations.end();}
Terrain flat(){return Terrain({{-200,-200,0},{400,-200,0},{-200,400,0},{400,400,0}},{{0,1,2},{1,3,2}});}
Terrain hole(){
    std::vector<Vec3> vertices;std::vector<std::array<std::size_t,3>> faces;
    for(int y=0;y<=12;++y)for(int x=0;x<=12;++x)vertices.push_back({20.0*x,20.0*y,0});
    for(std::size_t y=0;y<12;++y)for(std::size_t x=0;x<12;++x){if(x>=4&&x<8&&y>=4&&y<8)continue;auto i=13*y+x;faces.push_back({i,i+1,i+13});faces.push_back({i+1,i+14,i+13});}
    return Terrain(vertices,faces);
}
Parameters params(){Parameters p;p.mode=DesignMode::surface;p.min_radius_m=20;p.min_straight_m=40;p.max_turn_degrees=90;return p;}
void tangent_length_tests(const Terrain& terrain,Parameters p){
    p.continuous_curves=true;
    auto rounded=[&](double first,double middle,double last,double second_turn=90.){
        SearchResult result;result.route={{0,0,0}};double heading=0;
        auto straight=[&](double length){
            const auto a=result.route.back();const auto count=static_cast<std::size_t>(std::ceil(length/3));
            for(std::size_t i=1;i<=count;++i){double distance=length*static_cast<double>(i)/count;result.route.push_back({a.x+distance*std::cos(heading),a.y+distance*std::sin(heading),0});}
        };
        auto arc=[&](double degrees){
            const auto a=result.route.back();const double sign=std::copysign(1.,degrees),radius=p.min_radius_m;
            const Vec3 center{a.x-sign*radius*std::sin(heading),a.y+sign*radius*std::cos(heading),0};
            const double start=std::atan2(a.y-center.y,a.x-center.x);const auto first_index=result.route.size()-1;
            const auto count=static_cast<std::size_t>(std::ceil(std::abs(degrees)));
            for(std::size_t i=1;i<=count;++i){const double angle=start+degrees*std::numbers::pi/180*static_cast<double>(i)/count;result.route.push_back({center.x+radius*std::cos(angle),center.y+radius*std::sin(angle),0});}
            result.curves.push_back({first_index,result.route.size()-1,center,degrees});heading+=degrees*std::numbers::pi/180;
        };
        straight(first);arc(90);straight(middle);arc(second_turn);straight(last);return result;
    };
    auto verify=[&](const SearchResult& result){return validate(result.route,terrain,p,result.curves);};
    check(verify(rounded(40,40,40)).checked_constraints_pass(),"Sampled tangent blocks were measured per edge");
    check(has(verify(rounded(39,40,40)),"straight_before_turn"),"Short initial advance was accepted");
    check(has(verify(rounded(40,39,40)),"straight_before_turn"),"Short advance between same-direction curves was accepted");
    check(has(verify(rounded(40,40,39)),"straight_before_turn"),"Short final advance was accepted");
    check(verify(rounded(40,0,40)).checked_constraints_pass(),"Contiguous same-direction curves acquired an unnecessary tangent");
    check(verify(rounded(0,0,0)).checked_constraints_pass(),"Curve lengths were treated as explicit straight advances");
    check(has(verify(rounded(40,0,40,-90)),"straight_before_turn"),"Contiguous opposite-direction curves bypassed the minimum tangent");
    check(verify(rounded(40,40,40,-90)).checked_constraints_pass(),"Adequately spaced opposite-direction curves were rejected");
    SearchResult direct;direct.route={{0,0,0},{20,0,0},{39,0,0}};
    check(has(verify(direct),"straight_before_turn"),"Short entirely straight advance was accepted");
    direct.route.back().x=40;check(verify(direct).checked_constraints_pass(),"Entirely straight advance at the minimum was rejected");
}
int main(int argc,char** argv){try {
    check(argc==2);std::string name=argv[1];auto p=params();auto terrain=flat();
    std::vector<Vec3> route{{0,0,0},{40,0,0}};
    for(int i=1;i<=90;++i){double a=(-90+i)*std::numbers::pi/180;route.push_back({40+20*std::cos(a),20+20*std::sin(a),0});}
    route.push_back({60,60,0});std::vector<AlignmentCurve> curves{{1,91,{40,20,0},90}};
    if(name=="geometry") {
        auto v=validate(route,terrain,p,curves);check(v.checked_constraints_pass(),"Valid rounded tangent/arc failed");check(v.not_evaluated.size()==5);
        check(has(validate(route,terrain,p),"unmodelled_turn"),"Unmarked microturns accepted");
        auto limited=p;limited.min_straight_m=41;check(has(validate(route,terrain,limited,curves),"straight_before_turn"));
        limited=p;limited.min_radius_m=21;check(has(validate(route,terrain,limited,curves),"curve_radius"));
        limited=p;limited.max_turn_degrees=89;check(has(validate(route,terrain,limited,curves),"max_turn_angle"),"Whole curve angle was not checked");
        tangent_length_tests(terrain,p);
    } else if(name=="tamper") {
        auto changed=route;changed[30].x+=2;check(!validate(changed,terrain,p,curves).checked_constraints_pass());
        auto changed_curves=curves;changed_curves[0].center.x+=3;check(!validate(route,terrain,p,changed_curves).checked_constraints_pass());
        changed_curves=curves;changed_curves[0].sweep_degrees=-90;check(!validate(route,terrain,p,changed_curves).checked_constraints_pass());
        changed_curves=curves;changed_curves[0].last=route.size()+1;check(has(validate(route,terrain,p,changed_curves),"alignment_metadata"));
        changed=route;changed.back().x+=10;check(has(validate(changed,terrain,p,curves),"curve_tangency"));
        changed=route;changed[40].z=10;check(has(validate(changed,terrain,p,curves),"grade_limit"));
    } else if(name=="hairpin") {
        route.resize(2);for(int i=1;i<=180;++i){double a=(-90+i)*std::numbers::pi/180;route.push_back({40+20*std::cos(a),20+20*std::sin(a),0});}
        route.push_back({0,40,0});curves={{1,181,{40,20,0},180}};p.max_turn_degrees=180;
        check(validate(route,terrain,p,curves).checked_constraints_pass(),"Spaced 180 degree arc rejected");
    } else if(name=="search"||name=="utm_search"||name=="space"||name=="budget"||name=="cancel") {
        terrain=hole();p.min_radius_m=10;p.min_straight_m=20;
        Vec3 offset{};if(name=="utm_search") {
            offset={780000,8440000,4300};std::vector<Vec3> vertices;for(auto q:terrain.vertices())vertices.push_back(to_world(q,offset));
            terrain=Terrain(vertices,std::vector<std::array<std::size_t,3>>(terrain.faces().begin(),terrain.faces().end()));
        }
        SearchOptions options;options.xy_spacing_m=10;RoadOptions road;road.heading_step_degrees=15;road.adaptive_resolution=false;
        if(name=="space")p.min_radius_m=1000;
        if(name=="budget")options.max_edges=5;
        std::stop_source source;if(name=="cancel")source.request_stop();
        auto result=plan_road(terrain,to_world({20,120,0},offset),to_world({220,120,0},offset),p,UnitCosts{},options,road,source.get_token());auto& r=result.surface;
        std::cout<<"status="<<static_cast<int>(r.status)<<" nodes="<<r.expanded<<" edges="<<r.edges_checked<<" curves="<<r.curves.size()<<'\n';
        if(name=="search"||name=="utm_search") {
            check(r.status==SearchStatus::found,"Constrained route around hole not found");check(!r.curves.empty());check(!result.earthwork);
            auto v=validate(r.route,terrain,p,r.curves);check(v.checked_constraints_pass(),"Generated route failed independent verification");
            check(r.route.front().x==20+offset.x&&r.route.front().y==120+offset.y&&r.route.back().x==220+offset.x&&r.route.back().y==120+offset.y,"A/B moved");
        } else if(name=="space")check(r.status!=SearchStatus::found&&r.route.empty(),"Radius was relaxed to force a path");
        else if(name=="budget")check(r.status==SearchStatus::budget_exceeded&&r.route.empty());
        else check(r.status==SearchStatus::cancelled&&r.route.empty());
    } else throw std::runtime_error("Unknown test");
    std::cout<<name<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
