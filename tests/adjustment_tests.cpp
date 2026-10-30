#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <stdexcept>
using namespace routemine;
void check(bool v,const char* message="assertion failed"){if(!v)throw std::runtime_error(message);}
bool has(const Validation& v,const char* code){return std::find(v.violations.begin(),v.violations.end(),code)!=v.violations.end();}
Parameters params(){Parameters p;p.continuous_curves=true;p.max_grade_percent=10;p.min_radius_m=50;p.min_straight_m=84;p.road_width_m=4;p.max_cut_m=0;p.max_fill_m=0;return p;}
int main(int argc,char** argv){try{
    check(argc==2);std::string test=argv[1];auto p=params();SearchOptions limits;limits.max_states=3000;limits.max_expansions=1000;limits.max_edges=9000;RoadOptions road;road.adaptive_resolution=false;
    if(test=="reversal"){
        Terrain t({{-200,-200,0},{400,-200,0},{-200,400,0},{400,400,0}},{{0,1,2},{1,3,2}});
        p.min_radius_m=30;p.min_straight_m=35;
        for(double tangent:{34.0,35.0,84.0}){
            std::vector<Vec3> route{{0,0,0}};std::vector<AlignmentCurve> curves;
            for(int i=1;i<=90;++i){double a=(-90+i)*std::numbers::pi/180;route.push_back({30*std::cos(a),30+30*std::sin(a),0});}
            curves.push_back({0,90,{0,30,0},90});route.push_back({30,30+tangent,0});auto first=route.size()-1;
            for(int i=1;i<=90;++i){double a=(180-i)*std::numbers::pi/180;route.push_back({60+30*std::cos(a),30+tangent+30*std::sin(a),0});}
            curves.push_back({first,route.size()-1,{60,30+tangent,0},-90});
            auto v=validate(route,t,p,curves);check(tangent<35?has(v,"straight_before_turn"):v.checked_constraints_pass(),"Reversal tangent not enforced");
        }
    }else if(test=="surface"||test=="earthwork"||test=="zero_diagnosis"||test=="automatic"||test=="corridor"){
        // A narrow sloping strip cannot fit a radius-30 detour. The direct route
        // becomes feasible only after the slope limit reaches the terrain slope.
        Terrain t({{0,-3,0},{100,-3,11},{0,3,0},{100,3,11}},{{0,1,2},{1,3,2}});
        p.mode=test=="earthwork"?DesignMode::earthwork:DesignMode::surface;
        if(test=="zero_diagnosis")p.max_grade_percent=0;
        limits.max_states=200;limits.max_expansions=100;limits.max_edges=800;
        std::vector<int> progress;
        auto planner=test=="corridor"?plan_surface_reference:test=="automatic"?plan_road_with_retries:plan_road_adjusted;
        auto r=planner(t,{0,0,0},{100,0,11},p,UnitCosts{},limits,road,{},[&](int value){
            check(progress.empty()||value>=progress.back(),"Retry progress went backward");progress.push_back(value);
        },{});
        check(progress.front()==0&&progress.back()==100);
        if(test=="automatic")check(r.route.adjustment_attempts.front().step=="Intento inicial");
        check(r.route.status==SearchStatus::found,"Adjusted sloping strip missing");
        check(r.parameters.max_grade_percent>=(test=="corridor"?p.max_grade_percent:11)&&r.parameters.max_grade_percent<=12);
        check(r.parameters.mode==p.mode&&r.parameters.max_cut_m==0&&r.parameters.max_fill_m==0&&r.parameters.road_width_m==p.road_width_m);
        check(validate(r.route.route,t,r.parameters,r.route.curves).checked_constraints_pass());
        check(r.route.estimate->validation.max_cut_m<1e-8&&r.route.estimate->validation.max_fill_m<1e-8);
        if(test=="corridor"){
            check(r.route.strategy=="surface_reference"&&!r.parameters.continuous_curves&&r.parameters.min_radius_m==0);
            check(!r.route.estimate->validation.not_evaluated.empty());
        }else check(r.route.adjustment_attempts.size()>2&&r.route.adjustment_report.find("No se demuestra que sea la unica")!=std::string::npos);
        if(test=="zero_diagnosis")check(r.route.adjustment_report.find("incompatible")!=std::string::npos);
    }else if(test=="unresolved"||test=="cancel"||test=="bounds"){
        Terrain t({{0,0,0},{50,0,0},{0,50,0},{200,0,100},{250,0,100},{200,50,100}},{{0,1,2},{3,4,5}});
        limits.max_states=40;limits.max_expansions=20;limits.max_edges=100;
        if(test=="bounds"){
            p.max_grade_percent=13;bool rejected=false;try{plan_road_adjusted(t,{1,1,0},{201,1,100},p,UnitCosts{},limits,road);}catch(const std::invalid_argument&){rejected=true;}check(rejected);
        }else{
            std::stop_source stop;std::vector<int> progress;
            auto r=plan_road_adjusted(t,{1,1,0},{201,1,100},p,UnitCosts{},limits,road,stop.get_token(),[&](int value){
                check(progress.empty()||value>=progress.back(),"Progress went backward");progress.push_back(value);
                if(test=="cancel"&&value>0)stop.request_stop();
            });
            check(r.route.status!=SearchStatus::found);
            if(test=="cancel")check(r.route.status==SearchStatus::cancelled);
            else{
                check(r.route.adjustment_attempts.size()>=7);
                bool changed_radius=false,changed_grade=false;double previous_straight=p.min_straight_m,previous_radius=p.min_radius_m,previous_grade=p.max_grade_percent;
                for(const auto& a:r.route.adjustment_attempts){
                    check(a.min_straight_m>=35&&a.min_radius_m>=30&&a.max_grade_percent<=12);
                    check(a.min_straight_m<=previous_straight&&a.min_radius_m<=previous_radius&&a.max_grade_percent>=previous_grade);
                    if(a.min_radius_m<previous_radius){changed_radius=true;check(a.min_straight_m==35);}
                    if(a.max_grade_percent>previous_grade){changed_grade=true;check(a.min_radius_m==30&&a.min_straight_m==35);}
                    previous_straight=a.min_straight_m;previous_radius=a.min_radius_m;previous_grade=a.max_grade_percent;
                }
                check(changed_radius&&changed_grade);check(r.route.adjustment_report.find("No se demuestra imposibilidad")!=std::string::npos);
            }
        }
    }else throw std::runtime_error("Unknown adjustment test");
    std::cout<<test<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
