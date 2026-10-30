#include "routemine/road_standards.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
using namespace routemine;
namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
void near(double actual,double expected){check(std::abs(actual-expected)<1e-9,"Unexpected road-standard value");}
template<class F>void rejects(F action){bool rejected=false;try{action();}catch(const std::invalid_argument&){rejected=true;}check(rejected,"Invalid standard input was accepted");}
}
int main(int argc,char** argv){try{
    check(argc==2,"Pass formula, altitude, profiles or invalid");const std::string test=argv[1];RoadStandardInput input;
    if(test=="formula"){
        auto result=recommend_road_standards(input);near(result.lateral_friction,.17);near(result.min_radius_m,400/(127*(.06+.17)));
        near(result.min_straight_s_m,27.8);near(result.min_straight_o_m,55.6);
        check(result.min_radius_m<30,"A provisional operational radius floor leaked into the standard formula");
        input.design_speed_kmh=30;result=recommend_road_standards(input);near(result.min_radius_m,900/(127*(.06+.17)));
        near(result.min_straight_s_m,42);near(result.min_straight_o_m,84);
        input.design_speed_kmh=40;result=recommend_road_standards(input);near(result.min_radius_m,1600/(127*(.06+.17)));
        near(result.min_straight_s_m,56);near(result.min_straight_o_m,111);
        input.design_speed_kmh=50;result=recommend_road_standards(input);near(result.lateral_friction,.16);near(result.min_straight_s_m,69);near(result.min_straight_o_m,139);
        input.design_speed_kmh=80;result=recommend_road_standards(input);near(result.lateral_friction,.14);near(result.min_straight_s_m,111);near(result.min_straight_o_m,222);
        input.design_speed_kmh=90;result=recommend_road_standards(input);near(result.min_straight_s_m,125);near(result.min_straight_o_m,250);
        input.design_speed_kmh=130;result=recommend_road_standards(input);near(result.min_straight_s_m,180);near(result.min_straight_o_m,362);
        input.design_speed_kmh=20;input.superelevation_percent=0;result=recommend_road_standards(input);near(result.min_radius_m,400/(127*.17));
        check(!result.source_references.empty(),"Recommendations lost their sources");
    }else if(test=="altitude"){
        input.design_speed_kmh=30;input.altitude_m=3000;auto at=recommend_road_standards(input);
        check(at.max_grade_percent.has_value()&&at.base_road_width_m.has_value(),"Applicable third-class table cells were omitted");near(*at.max_grade_percent,10);near(*at.base_road_width_m,6);
        input.altitude_m=3000.001;auto above=recommend_road_standards(input);near(*above.max_grade_percent,9);
        input.altitude_m=2999.999;near(*recommend_road_standards(input).max_grade_percent,10);
        input.design_speed_kmh=20;check(!recommend_road_standards(input).max_grade_percent,"Altitude invented a normative grade for 20 km/h");
        input.design_speed_kmh=40;input.terrain=DesignTerrain::flat;input.altitude_m=4000;near(*recommend_road_standards(input).max_grade_percent,8);
        input.terrain=DesignTerrain::rolling;near(*recommend_road_standards(input).max_grade_percent,9);
    }else if(test=="profiles"){
        auto result=recommend_road_standards(input);check(!result.tabulated_speed&&!result.max_grade_percent&&!result.base_road_width_m,"20 km/h was labelled a tabulated third-class profile");
        check(result.profile_label.find("Exploración 20")!=std::string::npos&&!result.diagnostics.empty(),"Provisional speed lacks its explanation");
        input.design_speed_kmh=30;result=recommend_road_standards(input);check(result.tabulated_speed,"30 km/h steep third class was omitted");near(*result.max_grade_percent,10);near(*result.base_road_width_m,6);
        input.design_speed_kmh=40;result=recommend_road_standards(input);check(!result.tabulated_speed&&!result.max_grade_percent&&!result.base_road_width_m,"Empty steep 40 km/h cells were filled automatically");
        input.terrain=DesignTerrain::rugged;result=recommend_road_standards(input);check(result.tabulated_speed,"40 km/h rugged third class was omitted");near(*result.max_grade_percent,10);near(*result.base_road_width_m,6);
        input.terrain=DesignTerrain::flat;result=recommend_road_standards(input);near(*result.max_grade_percent,8);near(*result.base_road_width_m,6.6);
        input.terrain=DesignTerrain::rolling;near(*recommend_road_standards(input).max_grade_percent,9);
        input.design_speed_kmh=80;result=recommend_road_standards(input);check(result.tabulated_speed,"80 km/h rolling third class was omitted");near(*result.max_grade_percent,7);near(*result.base_road_width_m,6.6);
        input.design_speed_kmh=30;check(!recommend_road_standards(input).tabulated_speed,"30 km/h rolling profile was invented");
        input.design_speed_kmh=50;input.terrain=DesignTerrain::rugged;check(!recommend_road_standards(input).max_grade_percent,"A grade cell overrode the applicable design-speed range");
    }else if(test=="invalid"){
        for(double speed:{0.,-20.,131.,35.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
            auto changed=input;changed.design_speed_kmh=speed;rejects([&]{recommend_road_standards(changed);});
        }
        for(double e:{-1.,9.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()}){
            auto changed=input;changed.superelevation_percent=e;rejects([&]{recommend_road_standards(changed);});
        }
        input.altitude_m=std::numeric_limits<double>::quiet_NaN();rejects([&]{recommend_road_standards(input);});input.altitude_m=0;
        input.superelevation_percent=8;check(recommend_road_standards(input).min_radius_m>0,"Absolute ice allowance was confused with the normal 6 percent limit");
        input.ice_risk=false;input.superelevation_percent=12;check(recommend_road_standards(input).min_radius_m>0,"Supported absolute mountain peralte was rejected");
        input.terrain=DesignTerrain::flat;rejects([&]{recommend_road_standards(input);});
    }else throw std::invalid_argument("Unknown road standards test");
    std::cout<<test<<": PASS\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
