#include "project_io.hpp"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <stdexcept>
namespace {
[[noreturn]] void fail(const char* message){throw std::invalid_argument(message);}
routemine::Vec3 point(const QJsonValue& value) {
    if(!value.isArray())fail("Project point must be an XYZ array");
    auto a=value.toArray();if(a.size()!=3)fail("Project point needs three coordinates");
    for(auto item:a)if(!item.isDouble()||!std::isfinite(item.toDouble()))fail("Project coordinates must be finite numbers");
    return {a[0].toDouble(),a[1].toDouble(),a[2].toDouble()};
}
QJsonArray xyz(routemine::Vec3 p){return {p.x,p.y,p.z};}
std::size_t index(const QJsonObject& obj,const char* key) {
    auto value=obj[key];double n=value.toDouble(-1);
    if(!value.isDouble()||n<0||n>1000000||std::floor(n)!=n)fail("Invalid project route index");return static_cast<std::size_t>(n);
}
QJsonObject routing(const routemine::SearchResult& result) {
    QJsonArray reference,spans,curves;for(auto p:result.reference_route)reference.append(xyz(p));
    for(const auto& c:result.curves)curves.append(QJsonObject{{"first",static_cast<double>(c.first)},{"last",static_cast<double>(c.last)},
        {"center",xyz(c.center)},{"sweep_degrees",c.sweep_degrees}});
    for(const auto& s:result.interventions)spans.append(QJsonObject{{"first",static_cast<double>(s.first)},{"last",static_cast<double>(s.last)},
        {"reference_first",static_cast<double>(s.reference_first)},{"reference_last",static_cast<double>(s.reference_last)},
        {"detour_m",s.detour_m},{"proposal_m",s.proposal_m},{"max_cut_m",s.max_cut_m},{"max_fill_m",s.max_fill_m},{"terrain_angle_degrees",s.terrain_angle_degrees}});
    QJsonArray sections;for(const auto& s:result.local_sections)sections.append(QJsonObject{{"first",static_cast<double>(s.first)},{"last",static_cast<double>(s.last)},
        {"max_grade_percent",s.max_grade_percent},{"min_radius_m",s.min_radius_m},{"min_straight_m",s.min_straight_m}});
    QJsonArray attempts;for(const auto& a:result.adjustment_attempts)attempts.append(QJsonObject{
        {"step",QString::fromStdString(a.step)},{"min_straight_m",a.min_straight_m},{"min_radius_m",a.min_radius_m},{"max_grade_percent",a.max_grade_percent},
        {"xy_spacing_m",a.xy_spacing_m},{"heading_step_degrees",a.heading_step_degrees},{"status",static_cast<int>(a.status)},
        {"expanded",static_cast<double>(a.expanded)},{"edges_checked",static_cast<double>(a.edges_checked)}});
    return {{"local_sections",sections},{"adjustment_attempts",attempts},{"adjustment_report",QString::fromStdString(result.adjustment_report)},{"strategy",QString::fromStdString(result.strategy)},{"effective_xy_spacing_m",result.effective_xy_spacing_m},
            {"effective_heading_step_degrees",result.effective_heading_step_degrees},{"reference_route",reference},{"interventions",spans},{"curves",curves}};
}

double review_number(const QJsonObject& obj,const char* key,double minimum,double maximum) {
    auto value=obj[key];double n=value.toDouble(-1);
    if(!value.isDouble()||!std::isfinite(n)||n<minimum||n>maximum)fail("Invalid design review numeric value");return n;
}
QJsonObject design_parameters(const routemine::Parameters& p){
    return {{"max_grade_percent",p.max_grade_percent},{"max_cut_m",p.max_cut_m},{"max_fill_m",p.max_fill_m},
        {"min_radius_m",p.min_radius_m},{"road_width_m",p.road_width_m},{"min_straight_m",p.min_straight_m},
        {"min_straight_same_direction_m",p.min_straight_same_direction_m},{"apply_dg_grade_guidance",p.apply_dg_grade_guidance},
        {"max_turn_degrees",p.max_turn_degrees},{"continuous_curves",p.continuous_curves},
        {"mode",p.mode==routemine::DesignMode::surface?"surface":"earthwork"},
        {"sample_spacing_m",p.sample_spacing_m},{"surface_tolerance_m",p.surface_tolerance_m},{"max_samples",static_cast<double>(p.max_samples)}};
}
routemine::Parameters read_design_parameters(const QJsonValue& value){
    if(!value.isObject())fail("Missing requested design review parameters");auto obj=value.toObject();routemine::Parameters p;
    p.max_grade_percent=review_number(obj,"max_grade_percent",0,100);
    p.max_cut_m=review_number(obj,"max_cut_m",0,1e9);p.max_fill_m=review_number(obj,"max_fill_m",0,1e9);
    p.min_radius_m=review_number(obj,"min_radius_m",0,1e9);p.road_width_m=review_number(obj,"road_width_m",0,1e9);
    p.min_straight_m=review_number(obj,"min_straight_m",0,1e9);p.max_turn_degrees=review_number(obj,"max_turn_degrees",1e-9,180);
    if(obj.contains("min_straight_same_direction_m"))p.min_straight_same_direction_m=review_number(obj,"min_straight_same_direction_m",0,1e9);
    if(obj.contains("apply_dg_grade_guidance")){if(!obj["apply_dg_grade_guidance"].isBool())fail("Invalid DG grade guidance flag");p.apply_dg_grade_guidance=obj["apply_dg_grade_guidance"].toBool();}
    p.sample_spacing_m=review_number(obj,"sample_spacing_m",1e-9,1e9);p.surface_tolerance_m=review_number(obj,"surface_tolerance_m",0,1e9);
    double samples=review_number(obj,"max_samples",2,1e9);if(std::floor(samples)!=samples)fail("Invalid design review sample budget");p.max_samples=static_cast<std::size_t>(samples);
    if(!obj["continuous_curves"].isBool())fail("Invalid design review curve mode");p.continuous_curves=obj["continuous_curves"].toBool();
    auto mode=obj["mode"];if(!mode.isString()||(mode.toString()!="surface"&&mode.toString()!="earthwork"))fail("Invalid design review mode");
    p.mode=mode.toString()=="surface"?routemine::DesignMode::surface:routemine::DesignMode::earthwork;
    if(routemine::alignment_enabled(p)&&p.min_radius_m<=0)fail("Active design review needs a positive radius");return p;
}
QJsonObject design_candidate(const routemine::SearchResult& candidate){
    QJsonArray route,curves;for(auto p:candidate.route)route.append(xyz(p));
    for(const auto& c:candidate.curves)curves.append(QJsonObject{{"first",static_cast<double>(c.first)},{"last",static_cast<double>(c.last)},
        {"center",xyz(c.center)},{"sweep_degrees",c.sweep_degrees}});
    return {{"route",route},{"curves",curves},{"strategy",QString::fromStdString(candidate.strategy)}};
}
routemine::SearchResult read_design_candidate(const QJsonValue& encoded,std::size_t& vertices){
    if(!encoded.isObject())fail("Invalid design candidate");auto obj=encoded.toObject();auto route=obj["route"],curves=obj["curves"];
    if(!route.isArray()||route.toArray().size()<2||route.toArray().size()>200000||!curves.isArray()||curves.toArray().size()>route.toArray().size())fail("Invalid or oversized design candidate geometry");
    vertices+=static_cast<std::size_t>(route.toArray().size());if(vertices>1000000)fail("Design review exceeds total vertex budget");
    routemine::SearchResult result;for(auto p:route.toArray())result.route.push_back(point(p));(void)routemine::measure(result.route);
    std::size_t previous=0;for(auto value:curves.toArray()){
        if(!value.isObject())fail("Invalid design candidate curve");auto c=value.toObject();auto first=index(c,"first"),last=index(c,"last");
        if(first<previous||first>=last||last>=result.route.size())fail("Invalid design candidate curve range");
        double sweep=review_number(c,"sweep_degrees",-180.00000001,180.00000001);if(std::abs(sweep)<1e-6)fail("Invalid design candidate curve sweep");
        result.curves.push_back({first,last,point(c["center"]),sweep});previous=last;
    }
    if(!obj["strategy"].isString()||obj["strategy"].toString().size()>128)fail("Invalid design candidate strategy");result.strategy=obj["strategy"].toString().toStdString();
    // Saved classification is only a claim. The owning viewer revalidates against
    // the loaded terrain and requested design parameters before painting blue.
    result.status=routemine::SearchStatus::found;return result;
}
QJsonObject design_review_json(const ViewerDesignReview& saved){
    QJsonArray sections,gaps,branches;
    for(const auto& s:saved.review.sections)sections.append(QJsonObject{{"first_station_m",s.first_station_m},{"last_station_m",s.last_station_m},{"candidate",design_candidate(s.candidate)}});
    for(const auto& g:saved.review.gaps)gaps.append(QJsonObject{{"first_station_m",g.first_station_m},{"last_station_m",g.last_station_m},{"start",xyz(g.start)},{"end",xyz(g.end)},{"reason",QString::fromStdString(g.reason)}});
    for(const auto& branch:saved.branches)branches.append(design_candidate(branch));
    QJsonObject obj{{"schema_version",1},{"candidate_branches",branches},{"requested_parameters",design_parameters(saved.parameters)},{"sections",sections},{"gaps",gaps},
        {"report",QString::fromStdString(saved.review.report)},{"connected",QJsonValue(QJsonValue::Null)},
        {"road_candidates",saved.road_candidates},{"max_intervention_length_m",saved.max_intervention_length_m}};
    if(saved.review.connected)obj["connected"]=design_candidate(*saved.review.connected);return obj;
}
ViewerDesignReview read_design_review(const QJsonValue& encoded){
    if(!encoded.isObject())fail("Invalid design review");auto review_obj=encoded.toObject();
    if(!review_obj["schema_version"].isDouble()||review_obj["schema_version"].toDouble()!=1)fail("Unsupported design review version");
    ViewerDesignReview saved;saved.parameters=read_design_parameters(review_obj["requested_parameters"]);
    if(review_obj.contains("road_candidates")){
        if(!review_obj["road_candidates"].isBool())fail("Invalid road candidate mode");saved.road_candidates=review_obj["road_candidates"].toBool();
    }
    if(review_obj.contains("max_intervention_length_m"))saved.max_intervention_length_m=review_number(review_obj,"max_intervention_length_m",1e-9,1e9);
    auto sections=review_obj["sections"],gaps=review_obj["gaps"];
    if(!sections.isArray()||sections.toArray().size()>128||!gaps.isArray()||gaps.toArray().size()>512)fail("Invalid or oversized design review sections");
    std::size_t vertices=0;
    if(review_obj.contains("candidate_branches")){
        auto branches=review_obj["candidate_branches"];
        if(!branches.isArray()||branches.toArray().size()>256)fail("Invalid or oversized road candidate branches");
        for(auto branch:branches.toArray())saved.branches.push_back(read_design_candidate(branch,vertices));
    }
    for(auto value:sections.toArray()){
        if(!value.isObject())fail("Invalid design review section");auto obj=value.toObject();
        double first=review_number(obj,"first_station_m",0,1e9),last=review_number(obj,"last_station_m",0,1e9);if(first>=last)fail("Invalid design review station range");
        saved.review.sections.push_back({read_design_candidate(obj["candidate"],vertices),first,last});
    }
    for(auto value:gaps.toArray()){
        if(!value.isObject())fail("Invalid design review gap");auto obj=value.toObject();
        double first=review_number(obj,"first_station_m",0,1e9),last=review_number(obj,"last_station_m",0,1e9);if(first>=last)fail("Invalid design review gap station range");
        if(!obj["reason"].isString()||obj["reason"].toString().size()>4096)fail("Invalid design review gap description");
        saved.review.gaps.push_back({first,last,point(obj["start"]),point(obj["end"]),obj["reason"].toString().toStdString()});
    }
    if(!review_obj["report"].isString()||review_obj["report"].toString().size()>100000)fail("Invalid design review report");saved.review.report=review_obj["report"].toString().toStdString();
    auto connected=review_obj["connected"];if(!connected.isNull()&&!connected.isUndefined())saved.review.connected=read_design_candidate(connected,vertices);
    return saved;
}
QString absolute_path(const QString& path) {
    QFileInfo info(path);auto canonical=info.canonicalFilePath();
    auto resolved=canonical.isEmpty()?QDir::cleanPath(info.absoluteFilePath()):canonical;
#ifdef Q_OS_WIN
    resolved=resolved.toCaseFolded();
#endif
    return resolved;
}
bool same_file(const QString& a,const QString& b) {
    if(absolute_path(a)==absolute_path(b))return true;
#ifdef Q_OS_WIN
    auto first=std::filesystem::path(a.toStdWString()),second=std::filesystem::path(b.toStdWString());
#else
    auto first=std::filesystem::u8path(a.toUtf8().toStdString()),second=std::filesystem::u8path(b.toUtf8().toStdString());
#endif
    std::error_code error;bool same=std::filesystem::equivalent(first,second,error);return !error&&same;
}
}
ViewerProject read_viewer_project(const QByteArray& bytes,const QString& project_path) {
    QJsonParseError error;auto document=QJsonDocument::fromJson(bytes,&error);
    if(error.error!=QJsonParseError::NoError||!document.isObject())fail("Invalid project JSON");
    auto obj=document.object();ViewerProject project;
    if(obj.contains("schema_version")) {
        if(!obj["schema_version"].isDouble()||obj["schema_version"].toDouble()!=1||(obj["software"].toString()!="RouteMine"&&obj["software"].toString()!="RouteTec"))fail("Unsupported RouteTec project version");
        if(!obj["configuration"].isString())fail("Missing effective configuration");
        std::istringstream input(obj["configuration"].toString().toStdString());project.settings=routemine::read_settings(input);
        if(!project.settings.contains("terrain_path"))fail("Project has no terrain_path");
        project.terrain_path=QString::fromStdString(project.settings.at("terrain_path"));
    } else {
        if(obj["software"].toString()!="RouteTec 3D Professional"||obj["version"].toString()!="visor-threejs-1.0")fail("Unrecognized RouteTec project");
        project.settings=routemine::routetec_interface_defaults();
        project.settings["min_radius_m"]="0";project.settings["min_straight_m"]="0";project.settings["max_turn_degrees"]="180";
        project.settings["continuous_curves"]="0";project.settings["max_cut_m"]="5";project.settings["max_fill_m"]="5";
        if(!obj["source_dxf"].isString())fail("Legacy project has no source_dxf");
        project.terrain_path=obj["source_dxf"].toString();
        for(const auto& key:{QString("point_a"),QString("point_b")}) {
            auto p=point(obj[key]);auto prefix=key=="point_a"?"start_":"finish_";
            auto put=[&](const char* axis,double v){std::ostringstream out;out.precision(17);out<<v;project.settings[std::string(prefix)+axis]=out.str();};
            put("x",p.x);put("y",p.y);put("z",p.z);
        }
        auto parameters=obj["parameters"];
        if(!parameters.isUndefined()&&!parameters.isObject())fail("Invalid legacy parameters");
        auto grade=parameters.toObject()["max_grade"];
        if(!grade.isUndefined()) {
            if(grade.isString())project.settings["max_grade_percent"]=grade.toString().toStdString();
            else if(grade.isDouble())project.settings["max_grade_percent"]=QString::number(grade.toDouble(),'g',17).toStdString();
            else fail("Invalid legacy grade");
        }
        project.warnings<<"Proyecto RouteTec importado. Radio, recta mínima y pendiente cero en curvas no se consideran certificados por los parámetros antiguos.";
    }
    if(project.terrain_path.isEmpty())fail("Project has no terrain file");
    if(QFileInfo(project.terrain_path).isRelative())project.terrain_path=QFileInfo(project_path).dir().absoluteFilePath(project.terrain_path);
    const auto route=obj["route"];
    if(!route.isNull()&&!route.isUndefined()) {
        if(!route.isArray()||route.toArray().size()>1000000)fail("Invalid or oversized project route");
        for(auto value:route.toArray())project.route.push_back(point(value));
        if(!project.route.empty())(void)routemine::measure(project.route);
    }
    if(obj.contains("routing")) {
        if(!obj["routing"].isObject())fail("Invalid routing metadata");auto info=obj["routing"].toObject();
        auto& r=project.route_info;r.strategy=info["strategy"].toString().toStdString();
        if(r.strategy!="surface_reference"&&r.strategy!="surface_adaptive"&&r.strategy!="tin_surface"&&r.strategy!="surface_alignment"&&r.strategy!="continuous_ramp"&&r.strategy!="earthwork_shortcuts")fail("Unsupported route strategy");
        if(info.contains("local_sections")){
            if(!info["local_sections"].isArray()||info["local_sections"].toArray().size()>1000)fail("Invalid local sections");
            std::size_t previous=0;for(auto value:info["local_sections"].toArray()){
                if(!value.isObject())fail("Invalid local section");auto s=value.toObject();auto first=index(s,"first"),last=index(s,"last");
                auto scalar=[&](const char* key){auto v=s[key];if(!v.isDouble()||!std::isfinite(v.toDouble()))fail("Invalid local section parameter");return v.toDouble();};
                double grade=scalar("max_grade_percent"),radius=scalar("min_radius_m"),straight=scalar("min_straight_m");
                if(first<previous||first>=last||last>=project.route.size()||grade<0||grade>12||radius<30||straight<35)fail("Invalid local section constraints");
                r.local_sections.push_back({first,last,grade,radius,straight});previous=last;
            }
        }
        if(info.contains("adjustment_report")){
            if(!info["adjustment_report"].isString()||info["adjustment_report"].toString().size()>100000)fail("Invalid adjustment report");
            r.adjustment_report=info["adjustment_report"].toString().toStdString();
        }
        if(info.contains("adjustment_attempts")){
            if(!info["adjustment_attempts"].isArray()||info["adjustment_attempts"].toArray().size()>32)fail("Invalid adjustment attempts");
            for(auto value:info["adjustment_attempts"].toArray()){
                if(!value.isObject())fail("Invalid adjustment attempt");auto a=value.toObject();
                auto scalar=[&](const char* key){auto v=a[key];double n=v.toDouble(-1);if(!v.isDouble()||!std::isfinite(n)||n<0)fail("Invalid adjustment value");return n;};
                double straight=scalar("min_straight_m"),radius=scalar("min_radius_m"),grade=scalar("max_grade_percent"),xy=scalar("xy_spacing_m"),heading=scalar("heading_step_degrees");
                double status=scalar("status"),expanded=scalar("expanded"),edges=scalar("edges_checked");
                if(straight<35||radius<30||grade>12||xy<=0||heading<1||heading>45||status>3||std::floor(status)!=status||expanded>1e10||edges>1e10||std::floor(expanded)!=expanded||std::floor(edges)!=edges||!a["step"].isString())fail("Invalid adjustment bounds");
                r.adjustment_attempts.push_back({a["step"].toString().toStdString(),straight,radius,grade,xy,heading,static_cast<routemine::SearchStatus>(static_cast<int>(status)),static_cast<std::size_t>(expanded),static_cast<std::size_t>(edges)});
            }
        }
        r.effective_xy_spacing_m=info["effective_xy_spacing_m"].toDouble(-1);
        r.effective_heading_step_degrees=info["effective_heading_step_degrees"].toDouble(-1);
        if(!std::isfinite(r.effective_xy_spacing_m)||r.effective_xy_spacing_m<=0||!std::isfinite(r.effective_heading_step_degrees)||r.effective_heading_step_degrees<0||r.effective_heading_step_degrees>45)fail("Invalid effective route resolution");
        if(!info["reference_route"].isArray()||!info["interventions"].isArray()||info["reference_route"].toArray().size()>1000000)fail("Invalid reference route");
        for(auto value:info["reference_route"].toArray())r.reference_route.push_back(point(value));
        if(info.contains("curves")) {
            if(!info["curves"].isArray()||info["curves"].toArray().size()>1000000)fail("Invalid alignment curves");
            for(auto value:info["curves"].toArray()) {
                if(!value.isObject())fail("Invalid alignment curve");auto c=value.toObject();
                if(!c["sweep_degrees"].isDouble()||!std::isfinite(c["sweep_degrees"].toDouble()))fail("Invalid curve sweep");
                r.curves.push_back({index(c,"first"),index(c,"last"),point(c["center"]),c["sweep_degrees"].toDouble()});
            }
        }
        for(auto value:info["interventions"].toArray()) {
            if(!value.isObject())fail("Invalid intervention span");auto span=value.toObject();
            r.interventions.push_back({index(span,"first"),index(span,"last"),index(span,"reference_first"),index(span,"reference_last")});
        }
        if(r.strategy=="earthwork_shortcuts"&&r.interventions.empty())fail("Earthwork route needs intervention spans");
        if(r.strategy!="earthwork_shortcuts"&&(!r.interventions.empty()||!r.reference_route.empty()))fail("Surface route cannot contain interventions");
    }
    if(obj.contains("interface")){
        if(!obj["interface"].isObject())fail("Invalid interface settings");auto ui=obj["interface"].toObject();
        if(!ui["intermediate_enabled"].isBool())fail("Invalid intermediate setting");project.ui.intermediate_enabled=ui["intermediate_enabled"].toBool();
        if(ui.contains("local_grade")){
            routemine::Parameters p;for(const auto* key:{"local_grade","local_radius","local_straight"})if(!ui[key].isDouble()||!std::isfinite(ui[key].toDouble()))fail("Invalid local design setting");
            p.max_grade_percent=ui["local_grade"].toDouble();p.min_radius_m=ui["local_radius"].toDouble();p.min_straight_m=ui["local_straight"].toDouble();
            if(p.max_grade_percent<0||p.max_grade_percent>12||p.min_radius_m<30||p.min_straight_m<35)fail("Local design settings outside allowed limits");project.ui.local_design=p;
        }
    }
    if(obj.contains("design_review"))project.design_review=read_design_review(obj["design_review"]);
    return project;
}
QByteArray write_viewer_project(const routemine::Configuration& config,const routemine::SearchResult* result,const ViewerUiState& ui,const ViewerDesignReview* review) {
    std::ostringstream settings;routemine::write_configuration(settings,config);
    QJsonObject obj{{"software","RouteTec"},{"schema_version",1},{"source_dxf",QString::fromStdString(config.terrain_path)},
                    {"configuration",QString::fromStdString(settings.str())},{"point_a",xyz(config.start)},{"point_b",xyz(config.finish)}};
    obj["parameters"]=QJsonObject{{"max_grade",config.parameters.max_grade_percent}};
    QJsonObject interface_settings{{"intermediate_enabled",ui.intermediate_enabled}};
    if(ui.local_design){const auto& p=*ui.local_design;interface_settings["local_grade"]=p.max_grade_percent;interface_settings["local_radius"]=p.min_radius_m;interface_settings["local_straight"]=p.min_straight_m;}
    obj["interface"]=interface_settings;
    obj["route"]=QJsonValue(QJsonValue::Null);obj["metrics"]=QJsonValue(QJsonValue::Null);
    if(result&&result->status==routemine::SearchStatus::found&&result->estimate) {
        const auto& estimate=*result->estimate;const auto& v=estimate.validation;
        if(!v.checked_constraints_pass())fail("Cannot save a route with failed constraints");
        QJsonArray route;for(auto p:result->route)route.append(xyz(p));obj["route"]=route;
        if(!result->strategy.empty())obj["routing"]=routing(*result);
        QJsonArray pending;for(const auto& item:v.not_evaluated)pending.append(QString::fromStdString(item));
        obj["metrics"]=QJsonObject{{"horizontal",v.metrics.horizontal_m},{"distance_3d",v.metrics.spatial_m},
            {"desnivel",config.finish.z-config.start.z},{"grade",v.metrics.max_grade_percent},
            {"max_cut_m",v.max_cut_m},{"max_fill_m",v.max_fill_m},{"cut_m3",estimate.cut_m3},{"fill_m3",estimate.fill_m3},
            {"objective",config.cost_profile=="length_only"?"length_weights":"configured_costs"},
            {"objective_value",estimate.total},{"checked_constraints_pass",v.checked_constraints_pass()},
            {"all_constraints_pass",v.all_constraints_pass()},{"not_evaluated",pending},
            {"route_status","CORREDOR PRELIMINAR - GEOMETRIA VEHICULAR SIN CERTIFICAR"}};
        auto metrics=obj["metrics"].toObject();metrics["base_grade_limit_percent"]=config.parameters.max_grade_percent;
        metrics["additional_grade_degrees"]=config.road_search.additional_grade_degrees;
        metrics["detour_ratio_threshold"]=config.road_search.detour_ratio;obj["metrics"]=metrics;
    }
    if(review){auto value=design_review_json(*review);(void)read_design_review(value);obj["design_review"]=value;}
    return QJsonDocument(obj).toJson(QJsonDocument::Indented);
}
void save_viewer_file(const QString& path,const QByteArray& bytes,const QStringList& protected_paths) {
    for(const auto& source:protected_paths)if(!source.isEmpty()&&same_file(path,source))
        fail("Output would overwrite an input file; choose another path");
    QSaveFile file(path);
    if(!file.open(QIODevice::WriteOnly)||file.write(bytes)!=bytes.size()||!file.commit())
        throw std::runtime_error(("Cannot save file: "+file.errorString()).toStdString());
}
routemine::Parameters effective_review_parameters(const routemine::Configuration& config,const routemine::Parameters& saved){
    auto result=saved;if(config.standards_mode=="custom")return result;
    const auto& effective=config.parameters;
    const auto standard=routemine::recommend_road_standards(config.design_context);
    result.max_grade_percent=std::min(result.max_grade_percent,effective.max_grade_percent);
    result.min_radius_m=std::max({result.min_radius_m,effective.min_radius_m,standard.min_radius_m});
    result.min_straight_m=std::max({result.min_straight_m,effective.min_straight_m,standard.min_straight_s_m});
    result.min_straight_same_direction_m=std::max({result.min_straight_same_direction_m,effective.min_straight_same_direction_m,standard.min_straight_o_m});
    result.road_width_m=std::max(result.road_width_m,effective.road_width_m);
    result.max_cut_m=std::min(result.max_cut_m,effective.max_cut_m);result.max_fill_m=std::min(result.max_fill_m,effective.max_fill_m);
    result.surface_tolerance_m=std::min(result.surface_tolerance_m,effective.surface_tolerance_m);
    result.sample_spacing_m=std::min(result.sample_spacing_m,effective.sample_spacing_m);
    result.max_samples=std::min(result.max_samples,effective.max_samples);
    result.continuous_curves=true;
    result.max_turn_degrees=std::min(result.max_turn_degrees,effective.max_turn_degrees);
    result.apply_dg_grade_guidance=true;
    return result;
}
