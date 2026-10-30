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
Settings routetec_interface_defaults(){
    return {{"road_profile","routetec_monolith"},{"vehicle_profile","haul_truck_unspecified"},
            {"cost_profile","length_only"},{"max_grade_percent","10"},
            {"min_straight_m","35"},{"min_radius_m","30"},{"max_turn_degrees","120"},
            {"continuous_curves","1"},{"earthwork_mode","0"},{"max_cut_m","2"},{"max_fill_m","2"},
            {"standards_mode","preliminary"},{"design_speed_kmh","20"},{"design_terrain","steep"},
            {"design_superelevation_percent","6"},{"design_ice_risk","1"},{"design_altitude_m","0"}};
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
    c.cost_profile=text("cost_profile",legacy?"custom":(c.road_profile=="routetec_monolith"?"length_only":"synthetic_example"));
    c.vehicle_profile=text("vehicle_profile","haul_truck_unspecified");
    if(c.vehicle_profile!="unspecified"&&c.vehicle_profile!="haul_truck_unspecified")throw std::invalid_argument("Vehicle geometry profiles are not implemented");
    if(c.road_profile!="custom"&&c.road_profile!="synthetic_example"&&c.road_profile!="mining_haul_provisional"&&c.road_profile!="routetec_monolith")throw std::invalid_argument("Unknown road_profile");
    if(c.cost_profile!="custom"&&c.cost_profile!="synthetic_example"&&c.cost_profile!="length_only")throw std::invalid_argument("Unknown cost_profile");
    if(c.road_profile=="custom")for(const auto& key:road_keys)required(key);
    const bool routetec=c.road_profile=="routetec_monolith";
    if(c.cost_profile=="custom")for(const auto& key:cost_keys)required(key);
    if(legacy)for(const auto* key:{"sample_spacing_m","xy_spacing_m","z_min","z_max","z_step_m"})required(key);
    for(const auto* key:{"start_x","start_y","start_z","finish_x","finish_y","finish_z"})required(key);
    c.start={take("start_x",0),take("start_y",0),take("start_z",0)};
    c.finish={take("finish_x",0),take("finish_y",0),take("finish_z",0)};
    c.standards_mode=text("standards_mode","custom");
    if(c.standards_mode!="custom"&&c.standards_mode!="preliminary"&&c.standards_mode!="dg2018")throw std::invalid_argument("El modo de criterios debe ser custom, preliminary o dg2018.");
    c.design_context.design_speed_kmh=take("design_speed_kmh",20);
    const auto terrain_type=text("design_terrain","steep");
    if(terrain_type=="flat")c.design_context.terrain=DesignTerrain::flat;
    else if(terrain_type=="rolling")c.design_context.terrain=DesignTerrain::rolling;
    else if(terrain_type=="rugged")c.design_context.terrain=DesignTerrain::rugged;
    else if(terrain_type=="steep")c.design_context.terrain=DesignTerrain::steep;
    else throw std::invalid_argument("Orografía no reconocida: use flat, rolling, rugged o steep.");
    c.design_context.superelevation_percent=take("design_superelevation_percent",6);
    const double ice=take("design_ice_risk",1);if(ice!=0&&ice!=1)throw std::invalid_argument("El riesgo de hielo debe ser 0 o 1.");c.design_context.ice_risk=ice==1;
    const double altitude=take("design_altitude_m",0);if(altitude<0)throw std::invalid_argument("La altitud de referencia debe ser positiva o 0 para usar la TIN.");
    c.design_altitude_auto=altitude==0;c.design_context.altitude_m=c.design_altitude_auto?terrain.bounds()[1].z:altitude;
    auto stored_path=text("terrain_path",path);c.terrain_path=path.empty()?stored_path:path;
    if(!path.empty()&&!stored_path.empty()&&path!=stored_path)c.warnings.push_back("Explicit terrain argument overrides stored terrain_path");
    auto& p=c.parameters;
    // Deliberately synthetic; none of these values constitutes a road standard.
    p.max_grade_percent=take("max_grade_percent",routetec?10:2);p.max_cut_m=take("max_cut_m",5);p.max_fill_m=take("max_fill_m",routetec?5:0);
    p.road_width_m=take("road_width_m",routetec?8:1);
    // Missing geometry in old saved configurations retains the old search explicitly.
    p.min_radius_m=take("min_radius_m",0);p.min_straight_m=take("min_straight_m",0);p.max_turn_degrees=take("max_turn_degrees",180);
    p.min_straight_same_direction_m=take("min_straight_same_direction_m",0);
    p.sample_spacing_m=take("sample_spacing_m",2);p.surface_tolerance_m=take("surface_tolerance_m",0.01);
    double mode=take("earthwork_mode",1);if(mode!=0&&mode!=1)throw std::invalid_argument("earthwork_mode must be 0 or 1");
    p.mode=mode==1?DesignMode::earthwork:DesignMode::surface;
    auto& k=c.costs;
    const bool length_only=c.cost_profile=="length_only";
    k.road_per_m2=take("road_per_m2",length_only?1:10);k.soil_cut_per_m3=take("soil_cut_per_m3",length_only?0:1);k.rock_cut_per_m3=take("rock_cut_per_m3",0);
    k.fill_placement_per_m3=take("fill_placement_per_m3",0);k.imported_material_per_m3=take("imported_material_per_m3",0);
    k.disposal_per_m3=take("disposal_per_m3",0);k.haul_per_m3_km=take("haul_per_m3_km",0);k.rock_fraction=take("rock_fraction",0);
    k.reusable_cut_fraction=take("reusable_cut_fraction",0);k.internal_haul_km=take("internal_haul_km",0);
    k.import_haul_km=take("import_haul_km",0);k.disposal_haul_km=take("disposal_haul_km",0);
    if(length_only&&(k.road_per_m2!=1||k.soil_cut_per_m3!=0||k.rock_cut_per_m3!=0||k.fill_placement_per_m3!=0||
        k.imported_material_per_m3!=0||k.disposal_per_m3!=0||k.haul_per_m3_km!=0||k.rock_fraction!=0||
        k.reusable_cut_fraction!=0||k.internal_haul_km!=0||k.import_haul_km!=0||k.disposal_haul_km!=0))
        throw std::invalid_argument("length_only has fixed unit length weights; use cost_profile=custom to set prices");
    auto budget=[&](const std::string& key,std::size_t fallback){double v=take(key,static_cast<double>(fallback));
        if(v<1||v>10000000||std::floor(v)!=v)throw std::invalid_argument("Invalid resource budget: "+key);return static_cast<std::size_t>(v);};
    p.max_samples=budget("max_samples",p.max_samples);
    auto& s=c.search;auto bounds=terrain.bounds();
    s.xy_spacing_m=take("xy_spacing_m",10);s.z_step_m=take("z_step_m",1);
    s.z_min=take("z_min",std::min({bounds[0].z,c.start.z,c.finish.z})-p.max_cut_m);
    s.z_max=take("z_max",std::max({bounds[1].z,c.start.z,c.finish.z})+p.max_fill_m);
    s.max_states=budget("max_states",s.max_states);s.max_expansions=budget("max_expansions",s.max_expansions);s.max_edges=budget("max_edges",s.max_edges);
    auto flag=[&](const char* key,bool fallback){double v=take(key,fallback?1:0);if(v!=0&&v!=1)throw std::invalid_argument(std::string("Expected 0 or 1: ")+key);return v==1;};
    p.continuous_curves=flag("continuous_curves",false);
    if(c.standards_mode=="dg2018")p.continuous_curves=true;
    // Consume old persisted flags, but the selected profile owns the policy.
    // A stale saved 0 must not bypass checks after switching into a DG profile.
    (void)flag("apply_dg_grade_guidance",c.standards_mode!="custom");
    p.apply_dg_grade_guidance=c.standards_mode!="custom"&&alignment_enabled(p);
    const auto standard=recommend_road_standards(c.design_context);
    if(c.standards_mode!="custom"){
        if(c.standards_mode=="dg2018"&&!standard.tabulated_speed)throw std::invalid_argument("La combinación de velocidad y orografía no está tabulada para tercera clase en DG-2018 (Tabla 204.01). Mantenga 20 km/h en Exploración preliminar, o seleccione una combinación tabulada; en escarpado corresponde 30 km/h.");
        if(alignment_enabled(p)){
            p.min_radius_m=std::max(p.min_radius_m,standard.min_radius_m);
            p.min_straight_m=std::max(p.min_straight_m,standard.min_straight_s_m);
            // In a derived profile this is a computed value, not a hidden
            // stronger limit retained from a previously selected speed.
            p.min_straight_same_direction_m=standard.min_straight_o_m;
        }else c.warnings.push_back("Geometría desactivada: los mínimos de radio y tangentes calculados no se aplican al modo anterior.");
        if(c.standards_mode=="dg2018"){
            if(standard.max_grade_percent)p.max_grade_percent=std::min(p.max_grade_percent,*standard.max_grade_percent);
            if(standard.base_road_width_m)p.road_width_m=std::max(p.road_width_m,*standard.base_road_width_m);
        }
        c.warnings.push_back(standard.profile_label);
        c.warnings.insert(c.warnings.end(),standard.diagnostics.begin(),standard.diagnostics.end());
        c.warnings.insert(c.warnings.end(),standard.source_references.begin(),standard.source_references.end());
        if(p.apply_dg_grade_guidance)c.warnings.push_back("DG-2018, pág. 172: se comprueban tramos continuos >10 % de hasta 180 m y el criterio conservador de 8 % en curvas R<50 m. Descansos, pendientes medias y longitud crítica del vehículo requieren evaluación adicional. Los límites de exploración 2× y de intervención 100 m son independientes y no autorizan rasantes normativas al doble de pendiente.");
    }
    c.road_search.adaptive_resolution=flag("adaptive_resolution",true);
    c.road_search.propose_earthwork=flag("propose_earthwork",true);
    c.road_search.detour_ratio=take("detour_ratio",5);
    c.road_search.heading_step_degrees=take("heading_step_degrees",7.5);
    c.road_search.additional_grade_degrees=take("additional_grade_degrees",0);
    c.road_search.max_shortcut_checks=budget("max_shortcut_checks",10000);
    c.road_search.max_ramp_candidates=budget("max_ramp_candidates",64);
    if(c.road_search.detour_ratio<5||c.road_search.heading_step_degrees<1||c.road_search.heading_step_degrees>45)
        throw std::invalid_argument("detour_ratio must be >=5; heading_step_degrees must be in [1,45]");
    if(c.road_search.additional_grade_degrees<0||c.road_search.additional_grade_degrees>10)
        throw std::invalid_argument("additional_grade_degrees must be in [0,10]");
    for(const auto* key:{"min_radius","min_straight","zero_grade_on_curves"})
        if(values.contains(key))throw std::invalid_argument(std::string("Unsupported geometric setting: ")+key);
    if(!values.empty())throw std::invalid_argument("Unknown key: "+values.begin()->first);
    for(double v:{p.max_grade_percent,p.max_cut_m,p.max_fill_m,p.road_width_m,p.min_radius_m,p.min_straight_m,p.min_straight_same_direction_m,p.max_turn_degrees,p.sample_spacing_m,p.surface_tolerance_m,
        k.road_per_m2,k.soil_cut_per_m3,k.rock_cut_per_m3,k.fill_placement_per_m3,k.imported_material_per_m3,k.disposal_per_m3,
        k.haul_per_m3_km,k.rock_fraction,k.reusable_cut_fraction,k.internal_haul_km,k.import_haul_km,k.disposal_haul_km})
        if(!std::isfinite(v)||v<0)throw std::invalid_argument("Negative/nonfinite configuration value");
    if(p.max_grade_percent<0||p.max_grade_percent>100||p.road_width_m<=0||p.sample_spacing_m<=0||p.max_samples<2||k.rock_fraction>1||k.reusable_cut_fraction!=0)
        throw std::invalid_argument("Invalid supported search configuration (reuse must be zero)");
    if(!std::isfinite(s.z_min)||!std::isfinite(s.z_max)||s.z_min>s.z_max||s.xy_spacing_m<=0||s.z_step_m<=0||s.max_states<2)
        throw std::invalid_argument("Invalid search precision/domain");
    if(p.max_turn_degrees<=0||p.max_turn_degrees>180||(alignment_enabled(p)&&p.min_radius_m<=0))
        throw std::invalid_argument("La geometria activa requiere radio minimo > 0 y angulo de giro entre 0 (exclusivo) y 180 grados.");
    if((c.road_profile!="custom"&&!routetec)||c.cost_profile=="synthetic_example")c.warnings.push_back("SYNTHETIC profiles: illustrative values, no approved vehicle/road profile or real prices");
    if(routetec)c.warnings.push_back("Desktop defaults (overridable): grade 10%, width 8 m, radius 30 m, straight advance 35 m, cut/fill limits 2 m; not an approved road or vehicle design");
    if(length_only)c.warnings.push_back("Length objective with configurable weights, not a monetary estimate; select custom and supply real costs for economic comparisons");
    if(c.vehicle_profile=="haul_truck_unspecified")c.warnings.push_back("Mining haul-road context only: truck model/dimensions and vehicle geometry are unspecified");
    c.warnings.push_back(p.continuous_curves?"Continuous ramp: radius, grade, cut/fill and separation of turns checked; straight advance and reversal tangent minimum checked; no accumulated turn limit":(alignment_enabled(p)?"Sampled tangent/arc alignment: radius, minimum tangents and isolated curve sweep checked":"Legacy search: radius, minimum tangents and maximum turn angle disabled"));
    c.warnings.push_back("NOT IMPLEMENTED: vehicle swept envelope, superelevation, transition spirals, zero grade on curves, geotechnical stability");
    if(!p.continuous_curves)c.warnings.push_back("Self-intersections not checked in legacy/tangent mode");
    c.warnings.push_back("road_width_m affects preliminary quantities and separation of ramp turns; full terrain/vehicle corridor clearance is not verified");
    return c;
}
void write_configuration(std::ostream& out,const Configuration& c){
    out<<std::setprecision(17)<<"config_version=1\nterrain_path="<<std::quoted(c.terrain_path)
       <<"\nroad_profile="<<c.road_profile<<"\nvehicle_profile="<<c.vehicle_profile<<"\ncost_profile="<<c.cost_profile<<'\n';
    for(const auto& w:c.warnings)out<<"# WARNING: "<<w<<'\n';
    auto put=[&](const char* key,auto v){out<<key<<'='<<v<<'\n';};
    put("standards_mode",c.standards_mode);put("design_speed_kmh",c.design_context.design_speed_kmh);
    const char* terrain_type=c.design_context.terrain==DesignTerrain::flat?"flat":c.design_context.terrain==DesignTerrain::rolling?"rolling":c.design_context.terrain==DesignTerrain::rugged?"rugged":"steep";
    put("design_terrain",terrain_type);put("design_superelevation_percent",c.design_context.superelevation_percent);
    put("design_ice_risk",c.design_context.ice_risk?1:0);put("design_altitude_m",c.design_altitude_auto?0:c.design_context.altitude_m);
    put("start_x",c.start.x);put("start_y",c.start.y);put("start_z",c.start.z);
    put("finish_x",c.finish.x);put("finish_y",c.finish.y);put("finish_z",c.finish.z);
    const auto& p=c.parameters;put("max_grade_percent",p.max_grade_percent);put("max_cut_m",p.max_cut_m);put("max_fill_m",p.max_fill_m);
    put("road_width_m",p.road_width_m);put("earthwork_mode",p.mode==DesignMode::earthwork?1:0);
    put("min_radius_m",p.min_radius_m);put("min_straight_m",p.min_straight_m);put("max_turn_degrees",p.max_turn_degrees);
    put("continuous_curves",p.continuous_curves?1:0);
    put("min_straight_same_direction_m",p.min_straight_same_direction_m);put("apply_dg_grade_guidance",p.apply_dg_grade_guidance?1:0);
    put("sample_spacing_m",p.sample_spacing_m);put("surface_tolerance_m",p.surface_tolerance_m);put("max_samples",p.max_samples);
    const auto& s=c.search;put("xy_spacing_m",s.xy_spacing_m);put("z_min",s.z_min);put("z_max",s.z_max);put("z_step_m",s.z_step_m);
    put("max_states",s.max_states);put("max_expansions",s.max_expansions);put("max_edges",s.max_edges);
    const auto& r=c.road_search;put("adaptive_resolution",r.adaptive_resolution?1:0);put("propose_earthwork",r.propose_earthwork?1:0);
    put("detour_ratio",r.detour_ratio);put("heading_step_degrees",r.heading_step_degrees);put("max_shortcut_checks",r.max_shortcut_checks);
    put("additional_grade_degrees",r.additional_grade_degrees);put("max_ramp_candidates",r.max_ramp_candidates);
    const auto& k=c.costs;put("road_per_m2",k.road_per_m2);put("soil_cut_per_m3",k.soil_cut_per_m3);put("rock_cut_per_m3",k.rock_cut_per_m3);
    put("fill_placement_per_m3",k.fill_placement_per_m3);put("imported_material_per_m3",k.imported_material_per_m3);put("disposal_per_m3",k.disposal_per_m3);
    put("haul_per_m3_km",k.haul_per_m3_km);put("rock_fraction",k.rock_fraction);put("reusable_cut_fraction",k.reusable_cut_fraction);
    put("internal_haul_km",k.internal_haul_km);put("import_haul_km",k.import_haul_km);put("disposal_haul_km",k.disposal_haul_km);
    if(!out)throw std::runtime_error("Configuration write failed");
}
}
