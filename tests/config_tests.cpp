#include "routemine/config.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace routemine;
void check(bool v){if(!v)throw std::runtime_error("assertion failed");}
void near(double a,double b){check(std::abs(a-b)<1e-9);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad);}
Terrain terrain(){return Terrain({{0,0,0},{100,0,3},{0,100,0}},{{0,1,2}});}
Settings basic(){std::istringstream in("start_x=0\nstart_y=0\nstart_z=0\nfinish_x=10\nfinish_y=0\nfinish_z=0\n");return read_settings(in);}
int main(int argc,char** argv){try{check(argc==2);std::string name=argv[1];
    if(name=="defaults"){
        auto c=resolve_configuration(terrain(),"terrain.obj",basic());
        near(c.parameters.max_grade_percent,2);near(c.parameters.max_cut_m,5);near(c.parameters.road_width_m,1);
        near(c.costs.road_per_m2,10);near(c.costs.soil_cut_per_m3,1);near(c.search.z_min,-5);near(c.search.z_max,3);
        check(c.road_profile=="mining_haul_provisional"&&c.vehicle_profile=="haul_truck_unspecified"&&!c.warnings.empty());
    }else if(name=="precedence"){
        auto v=basic();v["max_grade_percent"]="7.5";v["soil_cut_per_m3"]="42";v["xy_spacing_m"]="3";v["z_min"]="-1";
        auto c=resolve_configuration(terrain(),"t.obj",v);near(c.parameters.max_grade_percent,7.5);near(c.costs.soil_cut_per_m3,42);near(c.search.xy_spacing_m,3);near(c.search.z_min,-1);
        near(c.parameters.max_cut_m,5);
    }else if(name=="validation"){
        for(const auto& item:Settings{{"min_radius_m","25"},{"min_straight_m","500"},{"vehicle_profile","haul_truck"},{"max_grade_percent","nan"},{"soil_cut_per_m3","-1"},{"road_width_m","0"},{"max_states","1.5"},{"cost_profile","custom"},{"road_profile","custom"},{"reusable_cut_fraction","0.5"},{"typo","1"},{"config_version","2"}}){
            auto v=basic();v[item.first]=item.second;rejects([&]{resolve_configuration(terrain(),"t.obj",v);});}
        rejects([]{std::istringstream in("start_x=1\nstart_x=2\n");read_settings(in);});
        auto v=basic();v.erase("finish_z");rejects([&]{resolve_configuration(terrain(),"t.obj",v);});
    }else if(name=="persistence"){
        auto v=basic();v["soil_cut_per_m3"]="20";v["surface_tolerance_m"]="0.02";
        auto c=resolve_configuration(terrain(),"D:\\mine #1\\terrain.obj",v);std::ostringstream out;write_configuration(out,c);
        std::istringstream saved(out.str());auto d=resolve_configuration(terrain(),"",read_settings(saved));
        check(d.terrain_path==c.terrain_path);near(d.costs.soil_cut_per_m3,20);near(d.parameters.surface_tolerance_m,0.02);
        near(d.search.z_min,c.search.z_min);near(d.search.z_max,c.search.z_max);near(d.finish.x,c.finish.x);
        std::ostringstream again;write_configuration(again,d);check(again.str()==out.str());
    }else if(name=="legacy"){
        auto c=resolve_configuration(terrain(),"t.obj",basic());std::ostringstream out;write_configuration(out,c);
        std::istringstream in(out.str());auto values=read_settings(in);values["road_profile"]="custom";values["cost_profile"]="custom";
        auto legacy=resolve_configuration(terrain(),"t.obj",values,true);near(legacy.parameters.max_grade_percent,2);
        values.erase("max_cut_m");rejects([&]{resolve_configuration(terrain(),"t.obj",values,true);});
    }else throw std::runtime_error("unknown test");
    std::cout<<name<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
