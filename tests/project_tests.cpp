#include "project_io.hpp"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <limits>
#include <QTemporaryDir>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace routemine;
void check(bool v){if(!v)throw std::runtime_error("assertion failed");}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad);}
int main(int argc,char** argv){QCoreApplication app(argc,argv);try {
    check(argc==2);std::string name=argv[1];Terrain terrain({{0,0,0},{100,0,0},{0,100,0}},{{0,1,2}});
    auto values=routetec_interface_defaults();for(const auto& [key,value]:Settings{{"start_x","0"},{"start_y","0"},{"start_z","0"},{"finish_x","20"},{"finish_y","0"},{"finish_z","0"}})values[key]=value;
    auto config=resolve_configuration(terrain,"terrain.obj",values);
    if(name=="standards_review"){
        values["standards_mode"]="dg2018";values["design_speed_kmh"]="30";values["design_altitude_m"]="4000";config=resolve_configuration(terrain,"t.obj",values);
        Parameters weak;weak.max_grade_percent=20;weak.min_radius_m=weak.min_straight_m=weak.min_straight_same_direction_m=0;weak.road_width_m=1;weak.max_cut_m=weak.max_fill_m=100;weak.continuous_curves=false;weak.apply_dg_grade_guidance=false;
        const auto guarded=effective_review_parameters(config,weak);
        check(guarded.max_grade_percent==9&&guarded.min_radius_m>=900/(127*.23)&&guarded.min_straight_m==42&&guarded.min_straight_same_direction_m==84);
        check(guarded.continuous_curves&&guarded.apply_dg_grade_guidance&&guarded.road_width_m==8&&guarded.max_cut_m==2&&guarded.max_fill_m==2);
        weak.max_grade_percent=7;weak.min_radius_m=100;weak.min_straight_same_direction_m=200;const auto stronger=effective_review_parameters(config,weak);check(stronger.max_grade_percent==7&&stronger.min_radius_m==100&&stronger.min_straight_same_direction_m==200);
        config.standards_mode="custom";check(!effective_review_parameters(config,weak).apply_dg_grade_guidance);
    }else if(name=="design_review_roundtrip"||name=="design_review_invalid") {
        auto original=config.parameters;original.max_grade_percent=8;original.min_radius_m=30;original.min_straight_m=35;original.continuous_curves=true;original.min_straight_same_direction_m=55.6;original.apply_dg_grade_guidance=true;
        config.parameters.continuous_curves=false;config.parameters.min_radius_m=0;config.parameters.min_straight_m=0;config.parameters.max_turn_degrees=180;config.parameters.max_grade_percent=12;
        ViewerDesignReview saved;saved.parameters=original;saved.review.report="Dos tramos independientes; conexión pendiente.";
        SearchResult first;first.status=SearchStatus::found;first.strategy="continuous_ramp";first.route={{0,0,0},{20,0,0}};
        SearchResult second;second.status=SearchStatus::found;second.strategy="continuous_ramp";
        for(int i=0;i<=90;++i){double angle=(-90+i)*3.14159265358979323846/180;second.route.push_back({40+30*std::cos(angle),30+30*std::sin(angle),0});}
        second.curves={{0,90,{40,30,0},90}};
        saved.review.sections={{first,0,20},{second,40,87.1238898038469}};
        saved.review.gaps={{20,40,{20,0,0},{40,0,0},"Falta resolver el empalme."}};
        auto bytes=write_viewer_project(config,nullptr,{},&saved);
        auto project=read_viewer_project(bytes,"review.json");check(project.design_review.has_value());
        const auto& restored=*project.design_review;
        check(restored.parameters.continuous_curves&&restored.parameters.min_radius_m==30&&restored.parameters.min_straight_m==35&&restored.parameters.max_grade_percent==8);
        check(restored.parameters.min_straight_same_direction_m==55.6&&restored.parameters.apply_dg_grade_guidance);
        check(project.settings.at("min_radius_m")=="0"&&project.settings.at("max_grade_percent")=="12");
        check(restored.review.sections.size()==2&&restored.review.sections[0].candidate.route.size()==2&&restored.review.sections[1].candidate.route.size()==91);
        check(restored.review.sections[1].candidate.curves.size()==1&&restored.review.sections[1].candidate.curves[0].last==90);
        check(restored.review.gaps.size()==1&&restored.review.gaps[0].start.x==20&&restored.review.gaps[0].end.x==40);
        check(!restored.review.connected&&!restored.review.sections[0].candidate.estimate&&project.route.empty());
        check(validate(restored.review.sections[1].candidate.route,terrain,restored.parameters,restored.review.sections[1].candidate.curves).checked_constraints_pass());
        if(name=="design_review_roundtrip"){
            check(!restored.road_candidates&&restored.max_intervention_length_m==100);
            saved.road_candidates=true;saved.max_intervention_length_m=250;saved.branches={first,second};
            auto road_project=read_viewer_project(write_viewer_project(config,nullptr,{},&saved),"road-candidates.json");
            check(road_project.design_review->road_candidates&&road_project.design_review->max_intervention_length_m==250);
            check(road_project.design_review->branches.size()==2&&road_project.design_review->branches[1].route.size()==91&&road_project.design_review->branches[1].curves.size()==1);
            check(!road_project.design_review->branches[0].estimate);
            auto legacy_obj=QJsonDocument::fromJson(bytes).object();auto legacy_review=legacy_obj["design_review"].toObject();
            legacy_review.remove("road_candidates");legacy_review.remove("max_intervention_length_m");legacy_review.remove("candidate_branches");legacy_obj["design_review"]=legacy_review;
            auto legacy_review_project=read_viewer_project(QJsonDocument(legacy_obj).toJson(),"legacy-review.json");
            check(!legacy_review_project.design_review->road_candidates&&legacy_review_project.design_review->max_intervention_length_m==100&&legacy_review_project.design_review->branches.empty());
            saved.review.connected=first;project=read_viewer_project(write_viewer_project(config,nullptr,{},&saved),"complete.json");
            check(project.design_review->review.connected&&project.design_review->review.connected->route.size()==2&&!project.design_review->review.connected->estimate);
        }else{
            auto original_json=QJsonDocument::fromJson(bytes).object();
            auto reject_review=[&](const QJsonObject& review){auto obj=original_json;obj["design_review"]=review;rejects([&]{read_viewer_project(QJsonDocument(obj).toJson(),"bad.json");});};
            auto review=original_json["design_review"].toObject();auto parameters=review["requested_parameters"].toObject();parameters["min_radius_m"]=QJsonValue(QJsonValue::Null);review["requested_parameters"]=parameters;reject_review(review);
            review=original_json["design_review"].toObject();auto sections=review["sections"].toArray();auto section=sections[1].toObject();auto candidate=section["candidate"].toObject();auto curves=candidate["curves"].toArray();auto curve=curves[0].toObject();curve["last"]=91;curves[0]=curve;candidate["curves"]=curves;section["candidate"]=candidate;sections[1]=section;review["sections"]=sections;reject_review(review);
            review=original_json["design_review"].toObject();auto gaps=review["gaps"].toArray();auto gap=gaps[0].toObject();gap["last_station_m"]=10;gaps[0]=gap;review["gaps"]=gaps;reject_review(review);
            review=original_json["design_review"].toObject();review["sections"]=QString("not an array");reject_review(review);
            for(const auto& invalid:QJsonArray{0,-1,1e10,QJsonValue(QJsonValue::Null),QString("100")}){
                review=original_json["design_review"].toObject();review["max_intervention_length_m"]=invalid;reject_review(review);
            }
            review=original_json["design_review"].toObject();review["road_candidates"]=1;reject_review(review);
            review=original_json["design_review"].toObject();review["candidate_branches"]=QString("not an array");reject_review(review);
            QJsonValue branch_candidate=original_json["design_review"].toObject()["sections"].toArray()[0].toObject()["candidate"];
            QJsonArray too_many_branches;for(int i=0;i<257;++i)too_many_branches.append(branch_candidate);
            review=original_json["design_review"].toObject();review["candidate_branches"]=too_many_branches;reject_review(review);
            saved.max_intervention_length_m=std::numeric_limits<double>::infinity();rejects([&]{write_viewer_project(config,nullptr,{},&saved);});saved.max_intervention_length_m=100;
            saved.parameters.max_grade_percent=std::numeric_limits<double>::infinity();rejects([&]{write_viewer_project(config,nullptr,{},&saved);});
        }
    } else if(name=="corridor_roundtrip") {
        auto planned=plan_surface_reference(terrain,config.start,config.finish,config.parameters,config.costs,config.search,config.road_search);
        config.parameters=planned.parameters;config.search=planned.search;config.road_search=planned.road;
        check(planned.route.status==SearchStatus::found&&planned.route.strategy=="surface_reference");
        auto loaded=read_viewer_project(write_viewer_project(config,&planned.route),"corridor.json");
        auto restored=resolve_configuration(terrain,"terrain.obj",loaded.settings);
        check(!restored.parameters.continuous_curves&&restored.parameters.min_radius_m==0&&loaded.route_info.strategy=="surface_reference");
        check(validate(loaded.route,terrain,restored.parameters).checked_constraints_pass());
        config.parameters.mode=DesignMode::earthwork;const Vec3 middle{(config.start.x+config.finish.x)/2,(config.start.y+config.finish.y)/2,-1};planned.route.route={config.start,middle,config.finish};
        planned.route.estimate=estimate_cost(planned.route.route,terrain,config.parameters,config.costs);
        loaded=read_viewer_project(write_viewer_project(config,&planned.route),"optimized.json");
        restored=resolve_configuration(terrain,"terrain.obj",loaded.settings);check(restored.parameters.mode==DesignMode::earthwork);
        check(validate(loaded.route,terrain,restored.parameters).checked_constraints_pass());
    } else if(name=="roundtrip") {
        values["finish_x"]="40";config=resolve_configuration(terrain,"terrain.obj",values);
        auto result=plan_cpu(terrain,config.start,config.finish,config.parameters,config.costs,config.search);check(result.status==SearchStatus::found);
        auto bytes=write_viewer_project(config,&result);auto project=read_viewer_project(bytes,"project.json");
        check(project.route.size()==result.route.size());check(project.settings.at("max_grade_percent")=="10");
        auto restored=resolve_configuration(terrain,"terrain.obj",project.settings);std::ostringstream before,after;
        write_configuration(before,config);write_configuration(after,restored);check(before.str()==after.str());
        auto metrics=QJsonDocument::fromJson(bytes).object()["metrics"].toObject();check(!metrics["all_constraints_pass"].toBool());check(metrics["checked_constraints_pass"].toBool());
        ViewerUiState ui;ui.intermediate_enabled=true;ui.local_design=config.parameters;ui.local_design->max_grade_percent=8;
        auto with_ui=read_viewer_project(write_viewer_project(config,&result,ui),"project.json");
        check(with_ui.ui.intermediate_enabled&&with_ui.ui.local_design&&with_ui.ui.local_design->max_grade_percent==8);
        check(with_ui.ui.local_design->min_radius_m==30&&with_ui.ui.local_design->min_straight_m==35);
    } else if(name=="legacy") {
        auto project=read_viewer_project(R"({"software":"RouteTec 3D Professional","version":"visor-threejs-1.0","source_dxf":"terrain.dxf","point_a":[0,0,0],"point_b":[20,0,0],"route":[[0,0,0],[20,0,0]],"parameters":{"max_grade":"7.5","min_straight":500,"turn_grade":0,"algorithm":"operational_switchback"},"metrics":{"grade_verified":true}})","project.json");
        check(!project.warnings.isEmpty());auto restored=resolve_configuration(terrain,"terrain.dxf",project.settings);check(restored.parameters.max_grade_percent==7.5&&restored.parameters.min_radius_m==0);
        check(project.route.size()==2);
    } else if(name=="road_roundtrip") {
        Terrain hill({{0,-500,0},{20,-500,5.358983848622454},{40,-500,0},{0,500,0},{20,500,5.358983848622454},{40,500,0}},{{0,1,3},{1,4,3},{1,2,4},{2,5,4}});
        values["finish_x"]="40";values["max_grade_percent"]="2";values["max_cut_m"]="6";values["max_fill_m"]="6";values["xy_spacing_m"]="5";
        values["min_radius_m"]="0";values["min_straight_m"]="0";values["max_turn_degrees"]="180";
        values["continuous_curves"]="0";
        auto c=resolve_configuration(hill,"hill.obj",values);auto alternatives=plan_road(hill,c.start,c.finish,c.parameters,c.costs,c.search,c.road_search);check(alternatives.earthwork.has_value());
        auto bytes=write_viewer_project(c,&*alternatives.earthwork);auto project=read_viewer_project(bytes,"project.json");SearchResult restored=project.route_info;restored.route=project.route;
        auto recovered=resolve_configuration(hill,"hill.obj",project.settings);validate_interventions(restored,hill,recovered.parameters,recovered.road_search);
        check(restored.strategy=="earthwork_shortcuts"&&!restored.interventions.empty());check(restored.interventions[0].detour_m>5*restored.interventions[0].proposal_m);
        std::ostringstream dxf;write_route_dxf(dxf,restored.route,restored.interventions);check(dxf.str().find("POLYLINE\n8\nROUTETEC_CORTE\n")!=std::string::npos);
    } else if(name=="alignment_roundtrip") {
        Terrain large({{-200,-200,0},{400,-200,0},{-200,400,0},{400,400,0}},{{0,1,2},{1,3,2}});
        values["finish_x"]="60";values["finish_y"]="60";values["min_radius_m"]="20";values["min_straight_m"]="40";values["max_turn_degrees"]="90";
        values["continuous_curves"]="0";
        auto c=resolve_configuration(large,"large.obj",values);SearchResult result;result.strategy="surface_alignment";result.status=SearchStatus::found;
        result.effective_xy_spacing_m=5;result.effective_heading_step_degrees=7.5;result.route={{0,0,0},{40,0,0}};
        for(int i=1;i<=90;++i){double a=(-90+i)*3.14159265358979323846/180;result.route.push_back({40+20*std::cos(a),20+20*std::sin(a),0});}
        result.route.push_back({60,60,0});result.curves.push_back({1,91,{40,20,0},90});
        result.estimate=estimate_cost(result.route,large,c.parameters,c.costs,result.curves);
        auto project=read_viewer_project(write_viewer_project(c,&result),"project.json");auto recovered=resolve_configuration(large,"large.obj",project.settings);
        check(project.route_info.curves.size()==1&&project.route_info.curves[0].sweep_degrees==90);
        check(validate(project.route,large,recovered.parameters,project.route_info.curves).checked_constraints_pass());
        check(!validate(project.route,large,recovered.parameters).checked_constraints_pass());
        project.route_info.curves[0].center.x+=5;check(!validate(project.route,large,recovered.parameters,project.route_info.curves).checked_constraints_pass());
    } else if(name=="ramp_roundtrip") {
        Terrain large({{-100,-100,0},{100,-100,0},{-100,100,0},{100,100,0}},{{0,1,2},{1,3,2}});
        values["start_x"]="20";values["finish_x"]="0";values["finish_y"]="-20";values["min_radius_m"]="20";values["min_straight_m"]="84";values["max_turn_degrees"]="30";values["continuous_curves"]="1";
        auto c=resolve_configuration(large,"large.obj",values);SearchResult result;result.strategy="continuous_ramp";result.status=SearchStatus::found;result.effective_xy_spacing_m=10;result.effective_heading_step_degrees=7.5;result.route={{20,0,0}};
        for(int quarter=0;quarter<3;++quarter){auto first=result.route.size()-1;for(int i=1;i<=90;++i){double a=(quarter*90+i)*3.14159265358979323846/180;result.route.push_back({20*std::cos(a),20*std::sin(a),0});}result.curves.push_back({first,result.route.size()-1,{0,0,0},90});}
        result.route.back()=c.finish;result.estimate=estimate_cost(result.route,large,c.parameters,c.costs,result.curves);
        result.adjustment_report="Solicitado: radio 50; utilizado: radio 30. Primera ruta valida encontrada.";
        result.adjustment_attempts.push_back({"Reducir radio",35,30,10,5,3.75,SearchStatus::found,100,900});
        auto project=read_viewer_project(write_viewer_project(c,&result),"project.json");auto restored=resolve_configuration(large,"large.obj",project.settings);
        check(restored.parameters.continuous_curves&&project.route_info.strategy=="continuous_ramp"&&project.route_info.curves.size()==3);
        check(project.route_info.adjustment_attempts.size()==1&&project.route_info.adjustment_attempts[0].min_radius_m==30);
        check(project.route_info.adjustment_report==result.adjustment_report);
        check(validate(project.route,large,restored.parameters,project.route_info.curves).checked_constraints_pass());
        restored.parameters.continuous_curves=false;check(!validate(project.route,large,restored.parameters,project.route_info.curves).checked_constraints_pass());
    } else if(name=="local_roundtrip") {
        Terrain flat({{0,-100,0},{200,-100,0},{0,100,0},{200,100,0}},{{0,1,2},{1,3,2}});
        values["start_x"]="0";values["finish_x"]="120";values["finish_z"]="8";values["min_radius_m"]="50";values["min_straight_m"]="35";values["max_grade_percent"]="5";values["max_fill_m"]="20";values["earthwork_mode"]="1";values["continuous_curves"]="1";
        auto c=resolve_configuration(flat,"flat.obj",values);SearchResult result;result.status=SearchStatus::found;result.strategy="continuous_ramp";result.effective_xy_spacing_m=5;result.effective_heading_step_degrees=3.75;
        result.route={{0,0,0},{40,0,2},{80,0,6},{120,0,8}};result.local_sections={{1,2,12,30,35}};result.estimate=estimate_sectioned_route(result,flat,c.parameters,c.costs);
        auto recovered=read_viewer_project(write_viewer_project(c,&result),"p.json");recovered.route_info.route=recovered.route;
        check(recovered.route_info.local_sections.size()==1&&recovered.route_info.local_sections[0].max_grade_percent==12);
        check(estimate_sectioned_route(recovered.route_info,flat,c.parameters,c.costs).validation.checked_constraints_pass());
        recovered.route_info.route[1].z=3;rejects([&]{estimate_sectioned_route(recovered.route_info,flat,c.parameters,c.costs);});
    } else if(name=="invalid") {
        for(auto text:{"{",R"({"schema_version":2,"software":"RouteMine"})",R"({"software":"other"})",R"({"software":"RouteTec 3D Professional","version":"visor-threejs-1.0","source_dxf":"x.dxf","point_a":[0,0],"point_b":[1,1,1]})"})rejects([&]{read_viewer_project(text,"project.json");});
        auto obj=QJsonDocument::fromJson(write_viewer_project(config,nullptr)).object();obj["route"]=QString("bad");rejects([&]{read_viewer_project(QJsonDocument(obj).toJson(),"p.json");});
    } else if(name=="atomic") {
        QTemporaryDir dir;check(dir.isValid());auto source=dir.filePath("input.obj");save_viewer_file(source,"ORIGINAL",{});
        rejects([&]{save_viewer_file(dir.path()+"/./input.obj","REPLACED",{source});});
        QFile input(source);check(input.open(QIODevice::ReadOnly));check(input.readAll()=="ORIGINAL");input.close();
#ifdef Q_OS_WIN
        auto alias=dir.filePath("hardlink.obj");std::error_code link_error;
        std::filesystem::create_hard_link(std::filesystem::path(source.toStdWString()),std::filesystem::path(alias.toStdWString()),link_error);
        if(!link_error)rejects([&]{save_viewer_file(alias,"REPLACED",{source});});
#endif
        auto dest=dir.filePath("project.json");save_viewer_file(dest,write_viewer_project(config,nullptr),{source});save_viewer_file(dest,write_viewer_project(config,nullptr),{source});
        QFile saved(dest);check(saved.open(QIODevice::ReadOnly));check(!read_viewer_project(saved.readAll(),dest).settings.empty());
    } else throw std::runtime_error("unknown test");
    std::cout<<name<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
