#include "routemine/road_standards.hpp"
#include <array>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace routemine {
namespace {
bool at_speed(double speed,double row){return std::abs(speed-row)<1e-8;}
double friction(double speed){
    // The third-class table explicitly labels the first row "30 (or less)".
    if(speed<=30)return .17;
    constexpr std::array<std::pair<double,double>,10> rows{{
        {40,.17},{50,.16},{60,.15},{70,.14},{80,.14},
        {90,.13},{100,.12},{110,.11},{120,.09},{130,.08}}};
    for(auto [row,value]:rows)if(at_speed(speed,row))return value;
    throw std::invalid_argument("La velocidad no tiene un coeficiente DG-2018 verificado: use 30 km/h o menos, o una velocidad tabulada entre 40 y 130 km/h en pasos de 10.");
}
bool third_class_speed(double speed,DesignTerrain terrain){
    if(terrain==DesignTerrain::steep)return at_speed(speed,30);
    if(terrain==DesignTerrain::rugged)return at_speed(speed,30)||at_speed(speed,40);
    for(double row:{40.,50.,60.,70.,80.})if(at_speed(speed,row))return true;
    return false;
}
std::string speed_text(double speed){std::ostringstream text;text<<speed;return text.str();}
}

RoadStandardRecommendations recommend_road_standards(const RoadStandardInput& input){
    const double speed=input.design_speed_kmh,e=input.superelevation_percent;
    if(!std::isfinite(speed)||speed<=0||speed>130)
        throw std::invalid_argument("La velocidad de diseño debe ser finita, mayor que cero y no superar 130 km/h para las tablas verificadas.");
    if(!std::isfinite(input.altitude_m))throw std::invalid_argument("La altitud de diseño debe ser finita.");
    switch(input.terrain){
        case DesignTerrain::flat:case DesignTerrain::rolling:case DesignTerrain::rugged:case DesignTerrain::steep:break;
        default:throw std::invalid_argument("La clase de orografía no es válida.");
    }
    const bool mountain=input.terrain==DesignTerrain::rugged||input.terrain==DesignTerrain::steep;
    const double normal_e=input.ice_risk?6:mountain?8:6;
    const double absolute_e=input.ice_risk?8:mountain?12:8;
    if(!std::isfinite(e)||e<0||e>absolute_e)
        throw std::invalid_argument("El peralte no es válido para el contexto seleccionado: debe estar entre 0 y "+speed_text(absolute_e)+" %.");

    RoadStandardRecommendations result;
    result.design_speed_kmh=speed;result.lateral_friction=friction(speed);
    result.min_radius_m=speed*speed/(127*(e/100+result.lateral_friction));
    if(!std::isfinite(result.min_radius_m)||result.min_radius_m<=0)
        throw std::invalid_argument("La velocidad y el peralte no permiten calcular un radio finito positivo.");
    result.tabulated_speed=third_class_speed(speed,input.terrain);
    result.profile_label=result.tabulated_speed?"DG-2018 · Tercera clase · "+speed_text(speed)+" km/h":
        "Exploración "+speed_text(speed)+" km/h (cálculos DG, no perfil normativo)";
    result.source_references={
        "DG-2018, Tabla 204.01, p. 97: rangos de velocidad de diseño por clase de carretera y orografía.",
        "DG-2018, p. 131: R = V² / [127 (e/100 + f)].",
        speed<=60?"DG-2018, Tabla 302.03, p. 132: fricción transversal máxima para tercera clase; 30 km/h o menos: 0,17.":
                  "DG-2018, Tabla 302.02, p. 129: coeficientes generales de fricción transversal; no amplían el rango de tercera clase.",
        "DG-2018, Tabla 302.01, p. 127: tangentes entre curvas; Lmín,S = 1,39 V; Lmín,O = 2,78 V.",
        "DG-2018, Tabla 304.05, p. 196: peraltes máximos normales y absolutos según contexto; con hielo: 6 % normal, 8 % absoluto."
    };

    // Use the published rounded values, not a new rounding of the formulas.
    constexpr std::array<std::array<double,3>,11> tangents{{
        {30,42,84},{40,56,111},{50,69,139},
        {60,83,167},{70,97,194},{80,111,222},
        {90,125,250},{100,139,278},{110,153,306},
        {120,167,333},{130,180,362}}};
    bool tangent_row=false;
    for(auto row:tangents)if(at_speed(speed,row[0])){
        result.min_straight_s_m=row[1];result.min_straight_o_m=row[2];tangent_row=true;break;
    }
    if(!tangent_row){
        result.min_straight_s_m=1.39*speed;result.min_straight_o_m=2.78*speed;
        result.diagnostics.push_back("Las rectas se calculan con 1,39 V y 2,78 V; no se presentan como valores de una fila tabulada verificada para esta velocidad.");
    }
    if(!result.tabulated_speed){
        result.diagnostics.push_back("La velocidad y la orografía elegidas no forman un perfil de tercera clase tabulado en la Tabla 204.01. En particular, 20 km/h no figura en sus rangos de diseño.");
        result.diagnostics.push_back("No se asignan pendiente máxima ni ancho normativos a esta combinación. Los cálculos de radio y recta son referencias para exploración.");
    }else{
        double grade=0;
        if(at_speed(speed,30))grade=10;
        else if(at_speed(speed,40))grade=input.terrain==DesignTerrain::flat?8:input.terrain==DesignTerrain::rolling?9:10;
        else if(at_speed(speed,50)||at_speed(speed,60))grade=8;
        else grade=7; // Verified 70 and 80 km/h rows, flat/rolling third class.
        if(mountain&&input.altitude_m>3000){
            grade-=1;
            result.diagnostics.push_back("Terreno accidentado o escarpado a más de 3000 m: la pendiente máxima tabulada se reduce en un punto porcentual.");
        }
        result.max_grade_percent=grade;
        result.base_road_width_m=mountain?6:6.6;
        result.source_references.push_back("DG-2018, p. 170 y Tabla 303.01, p. 171: pendientes máximas; reducción por altitud superior a 3000 m en terreno accidentado o escarpado.");
        result.source_references.push_back("DG-2018, Tabla 304.01, p. 191: ancho de calzada para tercera clase según velocidad y orografía; no incluye bermas ni sobreancho de curva.");
    }
    if(e>normal_e)
        result.diagnostics.push_back("El peralte seleccionado supera el máximo normal de "+speed_text(normal_e)+" % de este contexto y usa el margen absoluto excepcional; requiere justificación.");
    result.diagnostics.push_back("El radio se calcula con el peralte y la fricción indicados; no es un radio redondeado de la tabla ni una comprobación de la maniobra de un vehículo específico.");
    result.diagnostics.push_back("Estas recomendaciones no verifican sobreancho, envolvente del vehículo, transiciones de peralte, clotoides ni curvas verticales.");
    return result;
}

} // namespace routemine
