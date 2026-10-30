#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace routemine {
AdjustedRoadResult plan_surface_reference(const Terrain& terrain,Vec3 a,Vec3 b,const Parameters& requested,const UnitCosts& costs,
    const SearchOptions& resources,const RoadOptions& options,std::stop_token stop,ProgressCallback progress,std::function<void(const std::string&)> stage){
    AdjustedRoadResult output;output.parameters=requested;output.search=resources;output.road=options;
    auto& p=output.parameters;p.mode=DesignMode::surface;p.continuous_curves=false;p.min_radius_m=0;p.min_straight_m=0;p.min_straight_same_direction_m=0;p.apply_dg_grade_guidance=false;p.max_turn_degrees=180;
    output.road.propose_earthwork=false;output.road.adaptive_resolution=true;output.road.first_feasible=true;
    std::vector<double> grades{requested.max_grade_percent};
    for(double g:{2.,4.,6.,8.,10.,11.,12.})if(g>requested.max_grade_percent)grades.push_back(g);
    std::ostringstream report;report<<"Corredor sobre la TIN original. Curvas, radio y empalmes pendientes de diseño; no se usan para bloquear el corredor.\n";
    std::size_t expanded=0,edges=0;
    for(std::size_t i=0;i<grades.size();++i){
        p.max_grade_percent=grades[i];if(stage)stage("Buscando corredor sobre el terreno, pendiente máxima "+std::to_string(grades[i])+" %; curvas pendientes.");
        output.route=plan_road(terrain,a,b,p,costs,resources,output.road,stop,[&](int value){if(progress)progress(static_cast<int>((i*100+value)/grades.size()));}).surface;
        expanded+=output.route.expanded;edges+=output.route.edges_checked;
        report<<"Pendiente máxima "<<grades[i]<<" %: "<<(output.route.status==SearchStatus::found?"corredor encontrado":output.route.status==SearchStatus::budget_exceeded?"recursos agotados":"sin conexión encontrada")<<".\n";
        if(output.route.status==SearchStatus::found||output.route.status==SearchStatus::cancelled)break;
    }
    output.route.strategy="surface_reference";output.route.expanded=expanded;output.route.edges_checked=edges;output.route.adjustment_report=report.str();
    if(progress&&!stop.stop_requested())progress(100);return output;
}
AdjustedRoadResult plan_road_with_retries(const Terrain& terrain,Vec3 a,Vec3 b,const Parameters& p,const UnitCosts& costs,
    const SearchOptions& resources,const RoadOptions& road,std::stop_token stop,ProgressCallback progress,
    std::function<void(const std::string&)> stage){
    const bool eligible=p.continuous_curves&&p.max_grade_percent<=12&&p.min_radius_m>=30&&p.min_straight_m>=35;
    if(stage)stage("Intento inicial con los parámetros solicitados; explorando ambos extremos.");
    AdjustedRoadResult output;output.parameters=p;output.search=resources;output.road=road;
    output.route=plan_road(terrain,a,b,p,costs,resources,road,stop,[&](int value){if(progress)progress(eligible?value*30/100:value);}).surface;
    if(!eligible||output.route.status==SearchStatus::found||output.route.status==SearchStatus::cancelled){if(progress&&!stop.stop_requested())progress(100);return output;}
    auto original=std::move(output.route);
    output=plan_road_adjusted(terrain,a,b,p,costs,resources,road,stop,
        [&](int value){if(progress)progress(30+value*70/100);},stage);
    output.route.expanded+=original.expanded;output.route.edges_checked+=original.edges_checked;
    output.route.adjustment_report="El intento inicial no encontró una ruta validada. Se ejecutaron reintentos automáticos.\n"+output.route.adjustment_report;
    output.route.adjustment_attempts.insert(output.route.adjustment_attempts.begin(),
        {"Intento inicial",p.min_straight_m,p.min_radius_m,p.max_grade_percent,resources.xy_spacing_m,road.heading_step_degrees,original.status,original.expanded,original.edges_checked});
    if(original.status==SearchStatus::budget_exceeded&&output.route.status==SearchStatus::no_path)output.route.status=SearchStatus::budget_exceeded;
    return output;
}
AdjustedRoadResult plan_road_adjusted(const Terrain& terrain,Vec3 a,Vec3 b,const Parameters& requested,const UnitCosts& costs,
    const SearchOptions& resources,const RoadOptions& options,std::stop_token stop,ProgressCallback progress,
    std::function<void(const std::string&)> stage) {
    if(!requested.continuous_curves||!std::isfinite(requested.max_grade_percent)||requested.max_grade_percent<0||requested.max_grade_percent>12||
       !std::isfinite(requested.min_radius_m)||requested.min_radius_m<30||!std::isfinite(requested.min_straight_m)||requested.min_straight_m<35)
        throw std::invalid_argument("Los ajustes automaticos requieren pendiente 0-12 %, radio >=30 m y recta entre giros opuestos >=35 m.");
    struct Trial {Parameters p;SearchOptions search;RoadOptions road;std::string label;};
    std::vector<Trial> trials;
    auto p=requested;auto search=resources;auto road=options;road.adaptive_resolution=false;road.first_feasible=true;
    road.max_ramp_candidates=std::max(options.max_ramp_candidates,std::min<std::size_t>(512,options.max_ramp_candidates*2));
    // Vary resolution as well as effort: a larger state budget alone does not
    // recover headings or primitives absent from a discrete search.
    search.xy_spacing_m=std::max(0.25,resources.xy_spacing_m*0.75);
    search.max_states=std::max(resources.max_states,std::min<std::size_t>(2000000,resources.max_states*2));
    search.max_expansions=std::max(resources.max_expansions,std::min<std::size_t>(1000000,resources.max_expansions*2));
    search.max_edges=std::max(resources.max_edges,std::min<std::size_t>(10000000,resources.max_edges*2));
    road.heading_step_degrees=std::max(1.0,options.heading_step_degrees/2);
    auto add=[&](const char* label){trials.push_back({p,search,road,label});};
    add("1. Ampliar recursos y refinar direcciones, conservando la geometria");
    for(double target:{60.0,45.0,35.0})if(p.min_straight_m>target+1e-8){p.min_straight_m=target;add("2. Reducir recta al invertir el giro");}
    for(double target:{40.0,30.0})if(p.min_radius_m>target+1e-8){p.min_radius_m=target;add("3. Reducir radio minimo");}
    for(double target:{2.0,4.0,6.0,8.0,10.0,11.0,12.0})if(p.max_grade_percent<target-1e-8){p.max_grade_percent=target;add("4. Aumentar pendiente maxima");}
    // Final search changes discretization once more at the permitted geometry.
    search.xy_spacing_m=std::max(0.25,resources.xy_spacing_m*0.4);road.heading_step_degrees=std::max(1.0,options.heading_step_degrees/3);
    add("5. Refinamiento final con los limites permitidos");
    AdjustedRoadResult output;output.parameters=requested;output.search=resources;output.road=options;
    std::vector<AdjustmentAttempt> attempts;std::size_t expanded=0,edges=0;bool limited=false;
    std::ostringstream report;report<<std::setprecision(8)
        <<"Busqueda con ajustes permitidos. Orden: recursos, recta, radio y pendiente.\n"
        <<"Solicitado: recta "<<requested.min_straight_m<<" m; radio "<<requested.min_radius_m<<" m; pendiente "<<requested.max_grade_percent<<" %.\n"
        <<"Se conservan A/B, ancho "<<requested.road_width_m<<" m, modo "<<(requested.mode==DesignMode::surface?"sobre terreno":"corte/relleno")
        <<", corte maximo "<<requested.max_cut_m<<" m y relleno maximo "<<requested.max_fill_m<<" m.\n";
    if(progress)progress(0);
    for(std::size_t i=0;i<trials.size();++i){
        if(stop.stop_requested()){output.route.status=SearchStatus::cancelled;break;}
        const auto& t=trials[i];
        std::ostringstream label;label<<t.label<<" ("<<i+1<<"/"<<trials.size()<<"): recta "<<t.p.min_straight_m<<" m, radio "<<t.p.min_radius_m<<" m, pendiente "<<t.p.max_grade_percent<<" %.";
        if(stage)stage(label.str());
        auto r=plan_road(terrain,a,b,t.p,costs,t.search,t.road,stop,[&](int value){if(progress)progress(static_cast<int>((i*100+std::min(99,value))/trials.size()));}).surface;
        expanded+=r.expanded;edges+=r.edges_checked;limited|=r.status==SearchStatus::budget_exceeded;
        attempts.push_back({t.label,t.p.min_straight_m,t.p.min_radius_m,t.p.max_grade_percent,t.search.xy_spacing_m,t.road.heading_step_degrees,r.status,r.expanded,r.edges_checked});
        report<<i+1<<". "<<t.label<<": recta="<<t.p.min_straight_m<<", radio="<<t.p.min_radius_m<<", pendiente="<<t.p.max_grade_percent
              <<", XY="<<t.search.xy_spacing_m<<" m; "<<(r.status==SearchStatus::found?"ruta validada":r.status==SearchStatus::cancelled?"cancelado":r.status==SearchStatus::budget_exceeded?"presupuesto agotado":"sin candidato valido")
              <<"; expansiones="<<r.expanded<<"; aristas="<<r.edges_checked<<".\n";
        report<<"   Rechazos: pendiente "<<r.diagnostics.grade<<", cobertura "<<r.diagnostics.coverage<<", corte/relleno "<<r.diagnostics.earthwork<<", separacion/cruces "<<r.diagnostics.spacing<<", geometria "<<r.diagnostics.geometry<<". No prueban imposibilidad.\n";
        output.route=std::move(r);
        if(output.route.status==SearchStatus::cancelled)break;
        if(output.route.status==SearchStatus::found){
            // Independently verify against precisely the parameters to be saved.
            auto verified=validate(output.route.route,terrain,t.p,output.route.curves);
            if(!verified.checked_constraints_pass())throw std::runtime_error("La alternativa ajustada no supero la validacion independiente.");
            output.parameters=t.p;output.search=t.search;output.road=t.road;
            report<<"Primera ruta valida encontrada en esta secuencia. No se demuestra que sea la unica ruta posible.\n";break;
        }
    }
    if(output.route.status!=SearchStatus::found&&output.route.status!=SearchStatus::cancelled){
        output.route.status=limited?SearchStatus::budget_exceeded:SearchStatus::no_path;
        report<<"No se encontro una ruta con los ajustes ensayados. No se demuestra imposibilidad geometrica.\n";
    }
    if(requested.max_grade_percent==0&&std::abs(a.z-b.z)>1e-8)
        report<<"Diagnostico comprobado para los valores originales: pendiente 0 % incompatible con cotas A/B diferentes.\n";
    double dz=std::abs(a.z-b.z);
    if(dz>0)report<<"Longitud horizontal minima por desnivel al 12 %: "<<dz/0.12<<" m. Las vueltas deben caber en la superficie disponible y respetar radio y ancho.\n";
    if(output.route.status==SearchStatus::cancelled)report<<"Busqueda cancelada; no se aplicaron ajustes a los controles.\n";
    output.route.expanded=expanded;output.route.edges_checked=edges;output.route.adjustment_attempts=std::move(attempts);output.route.adjustment_report=report.str();
    if(progress&&output.route.status!=SearchStatus::cancelled)progress(100);
    return output;
}
}
