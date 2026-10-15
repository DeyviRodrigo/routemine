#include "routemine/config.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>
namespace routemine {
namespace {
std::string trim(std::string s){auto a=s.find_first_not_of(" \t\r");if(a==std::string::npos)return {};
    return s.substr(a,s.find_last_not_of(" \t\r")-a+1);}
double number(const std::string& text){std::istringstream input(text);double value;std::string extra;
    if(!(input>>value)||input>>extra||!std::isfinite(value))throw std::invalid_argument("Invalid finite number: "+text);return value;}
const std::vector<std::string> road_keys{"max_grade_percent","max_cut_m","max_fill_m","road_width_m","earthwork_mode"};
const std::vector<std::string> cost_keys{"road_per_m2","soil_cut_per_m3","rock_cut_per_m3","fill_placement_per_m3","imported_material_per_m3","disposal_per_m3","haul_per_m3_km","rock_fraction","reusable_cut_fraction","internal_haul_km","import_haul_km","disposal_haul_km"};
}
Settings read_settings(std::istream& input){
    Settings values;std::string line;std::size_t line_no=0;
    while(std::getline(input,line)){++line_no;line=trim(line);if(line.empty()||line.front()=='#')continue;
        auto at=line.find('=');if(at==std::string::npos)throw std::invalid_argument("Expected key=value at line "+std::to_string(line_no));
        auto key=trim(line.substr(0,at)),text=trim(line.substr(at+1));std::string value;
        if(key.empty()||key.find_first_of(" \t\r")!=std::string::npos)throw std::invalid_argument("Invalid key");
        if(!text.empty()&&text.front()=='"'){
            std::istringstream row(text);if(!(row>>std::quoted(value)))throw std::invalid_argument("Invalid quoted value");
            std::string tail;std::getline(row,tail);tail=trim(tail);if(!tail.empty()&&tail.front()!='#')throw std::invalid_argument("Trailing value");
        }else value=trim(text.substr(0,text.find('#')));
        if((value.empty()&&key!="terrain_path")||!values.emplace(key,value).second)throw std::invalid_argument("Empty/duplicate key: "+key);
    }
    if(input.bad())throw std::runtime_error("Settings read failed");return values;
}
Configuration resolve_configuration(const Terrain& terrain,std::string path,Settings values,bool legacy){
    auto text=[&](const std::string& key,std::string fallback){auto it=values.find(key);if(it==values.end())return fallback;
        auto result=it->second;values.erase(it);return result;};
    auto required=[&](const std::string& key){if(!values.contains(key))throw std::invalid_argument("Missing key: "+key);};
    auto take=[&](const std::string& key,double fallback){auto it=values.find(key);if(it==values.end())return fallback;
        double v=number(it->second);values.erase(it);return v;};
    if(take("config_version",1)!=1)throw std::invalid_argument("Unsupported config_version");
    Configuration c;
    c.road_profile=text("road_profile",legacy?"custom":"mining_haul_provisional");
    c.cost_profile=text("cost_profile",legacy?"custom":"synthetic_example");
    c.vehicle_profile=text("vehicle_profile","haul_truck_unspecified");
    if(c.vehicle_profile!="unspecified"&&c.vehicle_profile!="haul_truck_unspecified")throw std::invalid_argument("Vehicle geometry profiles are not implemented");
    if(c.road_profile!="custom"&&c.road_profile!="synthetic_example"&&c.road_profile!="mining_haul_provisional")throw std::invalid_argument("Unknown road_profile");
    if(c.cost_profile!="custom"&&c.cost_profile!="synthetic_example")throw std::invalid_argument("Unknown cost_profile");
    if(c.road_profile=="custom")for(const auto& key:road_keys)required(key);
    if(c.cost_profile=="custom")for(const auto& key:cost_keys)required(key);
    if(legacy)for(const auto* key:{"sample_spacing_m","xy_spacing_m","z_min","z_max","z_step_m"})required(key);
    for(const auto* key:{"start_x","start_y","start_z","finish_x","finish_y","finish_z"})required(key);
    c.start={take("start_x",0),take("start_y",0),take("start_z",0)};
    c.finish={take("finish_x",0),take("finish_y",0),take("finish_z",0)};
    auto stored_path=text("terrain_path",path);c.terrain_path=path.empty()?stored_path:path;
    if(!path.empty()&&!stored_path.empty()&&path!=stored_path)c.warnings.push_back("Explicit terrain argument overrides stored terrain_path");
    auto& p=c.parameters;
    // Deliberately synthetic; none of these values constitutes a road standard.
    p.max_grade_percent=take("max_grade_percent",2);p.max_cut_m=take("max_cut_m",5);p.max_fill_m=take("max_fill_m",0);
    p.road_width_m=take("road_width_m",1);p.min_radius_m=0;p.min_straight_m=0;
    p.sample_spacing_m=take("sample_spacing_m",2);p.surface_tolerance_m=take("surface_tolerance_m",0.01);
    double mode=take("earthwork_mode",1);if(mode!=0&&mode!=1)throw std::invalid_argument("earthwork_mode must be 0 or 1");
    p.mode=mode==1?DesignMode::earthwork:DesignMode::surface;
    auto& k=c.costs;
    k.road_per_m2=take("road_per_m2",10);k.soil_cut_per_m3=take("soil_cut_per_m3",1);k.rock_cut_per_m3=take("rock_cut_per_m3",0);
    k.fill_placement_per_m3=take("fill_placement_per_m3",0);k.imported_material_per_m3=take("imported_material_per_m3",0);
    k.disposal_per_m3=take("disposal_per_m3",0);k.haul_per_m3_km=take("haul_per_m3_km",0);k.rock_fraction=take("rock_fraction",0);
    k.reusable_cut_fraction=take("reusable_cut_fraction",0);k.internal_haul_km=take("internal_haul_km",0);
    k.import_haul_km=take("import_haul_km",0);k.disposal_haul_km=take("disposal_haul_km",0);
    auto budget=[&](const std::string& key,std::size_t fallback){double v=take(key,static_cast<double>(fallback));
        if(v<1||v>10000000||std::floor(v)!=v)throw std::invalid_argument("Invalid resource budget: "+key);return static_cast<std::size_t>(v);};
    p.max_samples=budget("max_samples",p.max_samples);
    auto& s=c.search;auto bounds=terrain.bounds();
    s.xy_spacing_m=take("xy_spacing_m",10);s.z_step_m=take("z_step_m",1);
    s.z_min=take("z_min",std::min({bounds[0].z,c.start.z,c.finish.z})-p.max_cut_m);
    s.z_max=take("z_max",std::max({bounds[1].z,c.start.z,c.finish.z})+p.max_fill_m);
    s.max_states=budget("max_states",s.max_states);s.max_expansions=budget("max_expansions",s.max_expansions);s.max_edges=budget("max_edges",s.max_edges);
    for(const auto* key:{"min_radius_m","min_radius","min_straight_m","min_straight","zero_grade_on_curves"})
        if(values.contains(key))throw std::invalid_argument(std::string("Unsupported geometric setting: ")+key);
    if(!values.empty())throw std::invalid_argument("Unknown key: "+values.begin()->first);
    for(double v:{p.max_grade_percent,p.max_cut_m,p.max_fill_m,p.road_width_m,p.sample_spacing_m,p.surface_tolerance_m,
        k.road_per_m2,k.soil_cut_per_m3,k.rock_cut_per_m3,k.fill_placement_per_m3,k.imported_material_per_m3,k.disposal_per_m3,
        k.haul_per_m3_km,k.rock_fraction,k.reusable_cut_fraction,k.internal_haul_km,k.import_haul_km,k.disposal_haul_km})
        if(!std::isfinite(v)||v<0)throw std::invalid_argument("Negative/nonfinite configuration value");
    if(p.max_grade_percent<=0||p.max_grade_percent>100||p.road_width_m<=0||p.sample_spacing_m<=0||p.max_samples<2||k.rock_fraction>1||k.reusable_cut_fraction!=0)
        throw std::invalid_argument("Invalid supported search configuration (reuse must be zero)");
    if(!std::isfinite(s.z_min)||!std::isfinite(s.z_max)||s.z_min>s.z_max||s.xy_spacing_m<=0||s.z_step_m<=0||s.max_states<2)
        throw std::invalid_argument("Invalid search precision/domain");
    if(c.road_profile!="custom"||c.cost_profile=="synthetic_example")c.warnings.push_back("SYNTHETIC profiles: illustrative values, no approved vehicle/road profile or real prices");
    if(c.vehicle_profile=="haul_truck_unspecified")c.warnings.push_back("Mining haul-road context only: truck model/dimensions and vehicle geometry are unspecified");
    c.warnings.push_back("NOT IMPLEMENTED: curve radius, minimum straights, vehicle swept width, zero grade on curves, self-intersections");
    c.warnings.push_back("road_width_m affects preliminary quantities/cost only; clearance is not verified");
    return c;
}
void write_configuration(std::ostream& out,const Configuration& c){
    out<<std::setprecision(17)<<"config_version=1\nterrain_path="<<std::quoted(c.terrain_path)
       <<"\nroad_profile="<<c.road_profile<<"\nvehicle_profile="<<c.vehicle_profile<<"\ncost_profile="<<c.cost_profile<<'\n';
    for(const auto& w:c.warnings)out<<"# WARNING: "<<w<<'\n';
    auto put=[&](const char* key,auto v){out<<key<<'='<<v<<'\n';};
    put("start_x",c.start.x);put("start_y",c.start.y);put("start_z",c.start.z);
    put("finish_x",c.finish.x);put("finish_y",c.finish.y);put("finish_z",c.finish.z);
    const auto& p=c.parameters;put("max_grade_percent",p.max_grade_percent);put("max_cut_m",p.max_cut_m);put("max_fill_m",p.max_fill_m);
    put("road_width_m",p.road_width_m);put("earthwork_mode",p.mode==DesignMode::earthwork?1:0);
    put("sample_spacing_m",p.sample_spacing_m);put("surface_tolerance_m",p.surface_tolerance_m);put("max_samples",p.max_samples);
    const auto& s=c.search;put("xy_spacing_m",s.xy_spacing_m);put("z_min",s.z_min);put("z_max",s.z_max);put("z_step_m",s.z_step_m);
    put("max_states",s.max_states);put("max_expansions",s.max_expansions);put("max_edges",s.max_edges);
    const auto& k=c.costs;put("road_per_m2",k.road_per_m2);put("soil_cut_per_m3",k.soil_cut_per_m3);put("rock_cut_per_m3",k.rock_cut_per_m3);
    put("fill_placement_per_m3",k.fill_placement_per_m3);put("imported_material_per_m3",k.imported_material_per_m3);put("disposal_per_m3",k.disposal_per_m3);
    put("haul_per_m3_km",k.haul_per_m3_km);put("rock_fraction",k.rock_fraction);put("reusable_cut_fraction",k.reusable_cut_fraction);
    put("internal_haul_km",k.internal_haul_km);put("import_haul_km",k.import_haul_km);put("disposal_haul_km",k.disposal_haul_km);
    if(!out)throw std::runtime_error("Configuration write failed");
}
}
