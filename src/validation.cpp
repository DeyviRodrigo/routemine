#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <iomanip>
#include <sstream>
#include <locale>
namespace routemine {
void validation_issue(Validation& v,std::span<const Vec3> route,const char* code,
                      std::size_t first,std::size_t last,const std::string& description) {
    if(std::find(v.violations.begin(),v.violations.end(),code)!=v.violations.end())return;
    v.violations.push_back(code);
    std::ostringstream out;out.imbue(std::locale::classic());out<<std::fixed<<std::setprecision(6);
    if(first<route.size()&&last<route.size()&&first<=last){
        double begin=0,end=0;
        for(std::size_t i=1;i<=last;++i){double h=std::hypot(route[i].x-route[i-1].x,route[i].y-route[i-1].y);end+=h;if(i<=first)begin+=h;}
        out<<"Tramo "<<first+1<<"–"<<last+1<<" (vértices; distancia horizontal desde A: "<<begin<<"–"<<end<<" m). ";
        auto a=route[first],b=route[last];
        out<<"Inicio XYZ: ("<<a.x<<", "<<a.y<<", "<<a.z<<"); fin XYZ: ("<<b.x<<", "<<b.y<<", "<<b.z<<").\n";
    }else out<<"Ubicación no disponible: los datos de la curva no identifican un tramo válido.\n";
    out<<description;v.diagnostics.push_back(out.str());
}
std::string validation_failure_message(const Validation& v) {
    std::string message="La ruta candidata no superó la validación.\nSe muestra la primera ubicación detectada para cada restricción incumplida:\n";
    for(const auto& detail:v.diagnostics)message+="\n"+detail+"\n";
    return message;
}
namespace {
std::string number(double value){std::ostringstream out;out.imbue(std::locale::classic());out<<std::setprecision(12)<<value;return out.str();}
}

Validation validate(std::span<const Vec3> route,const Terrain& terrain,const Parameters& p,std::span<const AlignmentCurve> curves) {
    for (double value:{p.max_grade_percent,p.max_cut_m,p.max_fill_m,p.min_radius_m,p.road_width_m,p.min_straight_m,p.min_straight_same_direction_m,p.max_turn_degrees,p.sample_spacing_m,p.surface_tolerance_m})
        if(!std::isfinite(value)||value<0) throw std::invalid_argument("Invalid nonnegative parameter");
    if(p.max_grade_percent<0 || p.max_grade_percent>100 || p.sample_spacing_m<=0 || p.max_samples<2)
        throw std::invalid_argument("Invalid grade or sampling limits");
    if(p.mode!=DesignMode::surface && p.mode!=DesignMode::earthwork) throw std::invalid_argument("Invalid mode");
    Validation r; r.metrics=measure(route);
    if(p.max_turn_degrees<=0||p.max_turn_degrees>180||(alignment_enabled(p)&&p.min_radius_m<=0))
        throw std::invalid_argument("Active alignment requires a positive radius and turn angle in (0,180]");
    validate_alignment(route,curves,p,r);
    auto unreported=[&](const char* code){return std::find(r.violations.begin(),r.violations.end(),code)==r.violations.end();};
    if(p.apply_dg_grade_guidance){
        // Conservative adoption of DG-2018 303.03.03 (printed p.172):
        // avoid grades above 8 percent on curves with radius below 50 m.
        std::size_t previous_end=0;
        for(const auto& curve:curves){
            if(curve.first<previous_end||curve.first>=curve.last||curve.last>=route.size()||!finite(curve.center))continue;
            previous_end=curve.last;const double radius=std::hypot(route[curve.first].x-curve.center.x,route[curve.first].y-curve.center.y);
            if(radius>=50-1e-6)continue;
            for(auto i=curve.first+1;i<=curve.last;++i){const double h=std::hypot(route[i].x-route[i-1].x,route[i].y-route[i-1].y);
                if(h<=1e-8)continue;const double grade=100*std::abs(route[i].z-route[i-1].z)/h;
                if(grade>8+1e-8&&unreported("dg_tight_curve_grade")){
                    validation_issue(r,route,"dg_tight_curve_grade",i-1,i,"Criterio conservador DG-2018 (§303.03.03, pág. 172): pendiente "+number(grade)+" %; máximo adoptado: 8 % en una curva de radio "+number(radius)+" m, inferior a 50 m. Reduzca la pendiente o amplíe el radio.");break;}
            }
        }
    }
    auto check_depth=[&](double projected,double ground,std::size_t segment){
        const double delta=projected-ground;
        r.max_cut_m=std::max(r.max_cut_m,-delta);r.max_fill_m=std::max(r.max_fill_m,delta);
        auto issue=[&](const char* code,const std::string& text){validation_issue(r,route,code,segment-1,segment,text);};
        if(p.mode==DesignMode::surface&&std::abs(delta)>p.surface_tolerance_m&&unreported("surface_separation"))
            issue("surface_separation","Separación respecto al terreno: "+number(std::abs(delta))+" m; tolerancia: "+number(p.surface_tolerance_m)+" m.");
        if(p.mode==DesignMode::earthwork){
            if(-delta>p.max_cut_m+1e-8&&unreported("cut_limit"))issue("cut_limit","Corte: "+number(-delta)+" m; máximo permitido: "+number(p.max_cut_m)+" m.");
            if(delta>p.max_fill_m+1e-8&&unreported("fill_limit"))issue("fill_limit","Relleno: "+number(delta)+" m; máximo permitido: "+number(p.max_fill_m)+" m.");
        }
    };
    double dg_steep_length=0;std::size_t dg_steep_first=0;
    for(std::size_t i=1;i<route.size();++i) {
        auto a=route[i-1], b=route[i];
        const double length=std::hypot(b.x-a.x,b.y-a.y);
        const double grade=std::abs(b.z-a.z)/length*100;
        if(grade>p.max_grade_percent+1e-8&&unreported("grade_limit"))validation_issue(r,route,"grade_limit",i-1,i,
            "Pendiente: "+number(grade)+" %; máximo permitido: "+number(p.max_grade_percent)+" %.");
        if(p.apply_dg_grade_guidance){
            if(grade>10+1e-8){if(dg_steep_length==0)dg_steep_first=i-1;dg_steep_length+=length;
                if(dg_steep_length>180+1e-6&&unreported("dg_long_steep_grade"))validation_issue(r,route,"dg_long_steep_grade",dg_steep_first,i,
                    "DG-2018 (§303.03.03, pág. 172): tramo continuo con pendiente superior al 10 % de "+number(dg_steep_length)+" m; máximo adoptado: 180 m. Introduzca un tramo de menor pendiente.");
            }else dg_steep_length=0;
        }
        auto missing_issue=[&]{validation_issue(r,route,"terrain_missing_or_ambiguous",i-1,i,
            "Cobertura del terreno: falta superficie TIN o existen cotas superpuestas incompatibles dentro de este tramo.");};
        auto profile=terrain.segment_profile(a,b);
        if(!profile){missing_issue();}
        else for(auto ground:*profile){
            if(r.samples>=p.max_samples)throw std::length_error("Face-boundary sampling budget exceeded");
            ++r.samples;
            const double t=std::hypot(ground.x-a.x,ground.y-a.y)/length;
            check_depth(std::lerp(a.z,b.z,std::clamp(t,0.0,1.0)),ground.z,i);
        }
        const double steps=std::max(1.0,std::ceil(length/p.sample_spacing_m));
        if(!std::isfinite(steps) || steps>=static_cast<double>(p.max_samples-r.samples))
            throw std::length_error("Sampling budget exceeded; choose an explicit coarser spacing");
        const auto count=static_cast<std::size_t>(steps);
        for(std::size_t j=0;j<=count;++j) {
            const double t=static_cast<double>(j)/static_cast<double>(count);
            const Vec3 q{std::lerp(a.x,b.x,t),std::lerp(a.y,b.y,t),std::lerp(a.z,b.z,t)};
            ++r.samples;
            auto z=terrain.elevation(q.x,q.y);
            if(!z) {missing_issue();continue;}
            check_depth(q.z,*z,i);
        }
    }
    // These need an alignment with arc/tangent primitives and a swept corridor.
    if(!alignment_enabled(p)){r.not_evaluated.push_back("curve_radius");r.not_evaluated.push_back("straight_before_turn");r.not_evaluated.push_back("max_turn_angle");}
    for(auto item:{"road_width_clearance","zero_grade_on_curves","superelevation","transition_spirals"})r.not_evaluated.push_back(item);
    if(!p.continuous_curves)r.not_evaluated.push_back("self_intersection");
    if(p.apply_dg_grade_guidance){
        // These require a separate longitudinal/operational study. Recording
        // them as pending must not invent a rolling-window acceptance rule.
        r.not_evaluated.push_back("dg_average_grade_over_2000m");
        r.not_evaluated.push_back("dg_rest_sections_after_3000m");
    }
    return r;
}
}
