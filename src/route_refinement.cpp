#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <iomanip>
#include <numbers>
#include <sstream>
#include <stdexcept>
namespace routemine {
namespace {
constexpr double pi=std::numbers::pi;
double distance(Vec3 a,Vec3 b){return std::hypot(b.x-a.x,b.y-a.y);}
std::vector<AlignmentCurve> slice_curves(std::span<const Vec3> route,std::span<const AlignmentCurve> curves,std::size_t first,std::size_t last){
    std::vector<AlignmentCurve> result;
    for(const auto& c:curves){auto lo=std::max(first,c.first),hi=std::min(last,c.last);if(lo>=hi)continue;
        double sweep=0,sign=std::copysign(1.0,c.sweep_degrees);
        for(auto i=lo+1;i<=hi;++i){auto a=route[i-1],b=route[i];sweep+=sign*std::remainder(std::atan2(b.y-c.center.y,b.x-c.center.x)-std::atan2(a.y-c.center.y,a.x-c.center.x),2*pi);}
        if(sweep>1e-8)result.push_back({lo-first,hi-first,c.center,sign*sweep*180/pi});
    }return result;
}
Parameters local_params(const Parameters& base,const LocalSection& section){auto p=base;p.max_grade_percent=section.max_grade_percent;p.min_radius_m=section.min_radius_m;p.min_straight_m=section.min_straight_m;return p;}
std::vector<LocalSection> regions(const SearchResult& route,const Parameters& base){
    std::vector<LocalSection> result;std::size_t cursor=0;
    auto ordinary=[&](std::size_t first,std::size_t last){result.push_back({first,last,base.max_grade_percent,base.min_radius_m,base.min_straight_m});};
    for(auto section:route.local_sections){
        if(section.first<cursor||section.first>=section.last||section.last>=route.route.size()||!std::isfinite(section.max_grade_percent)||section.max_grade_percent<0||section.max_grade_percent>12||!std::isfinite(section.min_radius_m)||section.min_radius_m<30||!std::isfinite(section.min_straight_m)||section.min_straight_m<35)throw std::invalid_argument("Invalid local section bounds");
        if(cursor<section.first)ordinary(cursor,section.first);result.push_back(section);cursor=section.last;
    }
    if(cursor+1<route.route.size())ordinary(cursor,route.route.size()-1);
    return result;
}
double heading_at(const SearchResult& route,std::size_t index){
    for(const auto& c:route.curves)if(index>=c.first&&index<=c.last)return std::atan2(route.route[index].y-c.center.y,route.route[index].x-c.center.x)+std::copysign(pi/2,c.sweep_degrees);
    auto a=route.route[index?index-1:index],b=route.route[index?index:index+1];return std::atan2(b.y-a.y,b.x-a.x);
}
}
CostEstimate estimate_sectioned_route(const SearchResult& route,const Terrain& terrain,const Parameters& base,const UnitCosts& costs){
    if(route.local_sections.empty())return estimate_cost(route.route,terrain,base,costs,route.curves);
    auto scopes=regions(route,base);auto envelope=base;
    for(auto s:scopes){envelope.max_grade_percent=std::max(envelope.max_grade_percent,s.max_grade_percent);envelope.min_radius_m=std::min(envelope.min_radius_m,s.min_radius_m);envelope.min_straight_m=std::min(envelope.min_straight_m,s.min_straight_m);}
    auto estimate=estimate_cost(route.route,terrain,envelope,costs,route.curves);
    for(auto s:scopes){auto curves=slice_curves(route.route,route.curves,s.first,s.last);
        auto v=validate(std::span(route.route).subspan(s.first,s.last-s.first+1),terrain,local_params(base,s),curves);
        if(!v.checked_constraints_pass())throw std::invalid_argument("Local/original section fails its own constraints");
    }
    // A reversal crossing a section boundary uses the stricter adjacent straight
    // requirement; relaxing a local segment cannot silently relax its neighbors.
    for(std::size_t i=1;i<route.curves.size();++i){auto prev=route.curves[i-1],next=route.curves[i];if(prev.sweep_degrees*next.sweep_degrees>=0)continue;
        double minimum=0;for(auto s:scopes)if(s.last>=prev.last&&s.first<=next.first)minimum=std::max(minimum,s.min_straight_m);
        if(distance(route.route[prev.last],route.route[next.first])+1e-6<minimum)throw std::invalid_argument("Reversal join fails adjacent section straight requirement");
    }
    return estimate;
}
RouteRefinement optimize_route_section(const Terrain& terrain,const SearchResult& original,std::size_t first,std::size_t last,
    const Parameters& base,const Parameters& local,const UnitCosts& costs,const SearchOptions& resources,const RoadOptions& options,
    std::stop_token stop,ProgressCallback progress){
    if(first>=last||last>=original.route.size()||!base.continuous_curves||!original.interventions.empty())throw std::invalid_argument("Select two ordered points on a continuous route");
    if(!std::isfinite(local.max_grade_percent)||local.max_grade_percent<0||local.max_grade_percent>12||!std::isfinite(local.min_radius_m)||local.min_radius_m<30||!std::isfinite(local.min_straight_m)||local.min_straight_m<35)throw std::invalid_argument("Local parameters require grade 0-12%, radius >=30 m and reversal straight >=35 m");
    auto previous=estimate_sectioned_route(original,terrain,base,costs);
    // Only these three parameters can differ locally. All physical depth/width
    // and sampling contracts come from the parent project.
    auto p=base;p.max_grade_percent=local.max_grade_percent;p.min_radius_m=local.min_radius_m;p.min_straight_m=local.min_straight_m;
    RouteRefinement output;std::ostringstream report;report<<std::setprecision(8)<<"Optimizacion local entre puntos "<<first+1<<" y "<<last+1<<".\n"
        <<"Parametros locales: pendiente "<<p.max_grade_percent<<" %, radio "<<p.min_radius_m<<" m, recta al invertir el giro "<<p.min_straight_m<<" m.\n"
        <<"Se conservan los extremos, las restricciones de los otros tramos, ancho y limites de corte/relleno.\n";
    auto pose=options;pose.start_heading=heading_at(original,first);pose.finish_heading=heading_at(original,last);pose.first_feasible=false;pose.adaptive_resolution=false;
    std::size_t expanded=0,edges=0;if(progress)progress(0);
    for(int phase=0;phase<3;++phase){
        if(stop.stop_requested())break;auto limits=resources;limits.xy_spacing_m=resources.xy_spacing_m*(phase==0?1.0:phase==1?0.75:0.4);
        pose.heading_step_degrees=std::max(1.0,options.heading_step_degrees/(phase+1));
        auto section=plan_continuous_ramp(terrain,original.route[first],original.route[last],p,costs,limits,pose,stop,[&](int value){if(progress)progress((phase*100+std::min(99,value))/3);});
        expanded+=section.expanded;edges+=section.edges_checked;
        if(section.status==SearchStatus::cancelled)break;
        if(section.status!=SearchStatus::found){report<<"Intento "<<phase+1<<": "<<(section.status==SearchStatus::budget_exceeded?"recursos agotados":"sin candidato valido")<<".\n";continue;}
        SearchResult candidate=original;candidate.route.assign(original.route.begin(),original.route.begin()+first);
        candidate.route.insert(candidate.route.end(),section.route.begin(),section.route.end());candidate.route.insert(candidate.route.end(),original.route.begin()+last+1,original.route.end());
        auto new_last=first+section.route.size()-1;auto shift=[&](std::size_t index){return new_last+(index-last);};
        candidate.curves=slice_curves(original.route,original.curves,0,first);
        for(auto c:section.curves){c.first+=first;c.last+=first;candidate.curves.push_back(c);}
        for(auto c:slice_curves(original.route,original.curves,last,original.route.size()-1)){c.first+=new_last;c.last+=new_last;candidate.curves.push_back(c);}
        candidate.local_sections.clear();
        for(auto s:original.local_sections){
            if(s.last<=first)candidate.local_sections.push_back(s);
            else if(s.first>=last){s.first=shift(s.first);s.last=shift(s.last);candidate.local_sections.push_back(s);}
            else {if(s.first<first){auto left=s;left.last=first;candidate.local_sections.push_back(left);}if(s.last>last){s.first=new_last;s.last=shift(s.last);candidate.local_sections.push_back(s);}}
        }
        candidate.local_sections.push_back({first,new_last,p.max_grade_percent,p.min_radius_m,p.min_straight_m});
        std::sort(candidate.local_sections.begin(),candidate.local_sections.end(),[](auto a,auto b){return a.first<b.first;});
        try{candidate.estimate=estimate_sectioned_route(candidate,terrain,base,costs);}catch(const std::invalid_argument& e){report<<"Intento "<<phase+1<<": empalme o restriccion rechazados ("<<e.what()<<").\n";continue;}
        auto best=output.candidate?output.candidate->estimate->total:previous.total;
        if(candidate.estimate->total>=best-1e-6){report<<"Intento "<<phase+1<<": sin mejora del objetivo completo.\n";continue;}
        report<<"Intento "<<phase+1<<": mejora validada. Objetivo "<<previous.total<<" -> "<<candidate.estimate->total<<".\n";
        candidate.expanded=original.expanded+expanded;candidate.edges_checked=original.edges_checked+edges;output.candidate=std::move(candidate);
    }
    if(stop.stop_requested()){output.candidate.reset();report<<"Cancelado. Se conserva la ruta anterior.\n";}
    else if(!output.candidate)report<<"No se encontro una mejora valida. Se conserva la ruta anterior; no se demuestra optimalidad.\n";
    else {report<<"Mejor candidato encontrado en estos intentos. No se demuestra optimo global.\n";output.candidate->adjustment_report+=report.str();}
    output.report=report.str();if(progress&&!stop.stop_requested())progress(100);return output;
}
namespace {
SearchResult optimize_polyline_reference(const Terrain& terrain,const SearchResult& original,const Parameters& input,const UnitCosts& costs,
    const SearchOptions& resources,const RoadOptions& road,std::stop_token stop,ProgressCallback progress){
    auto p=input;p.mode=DesignMode::earthwork;p.continuous_curves=false;p.min_radius_m=p.min_straight_m=p.min_straight_same_direction_m=0;p.apply_dg_grade_guidance=false;p.max_turn_degrees=180;
    auto best=original;best.estimate=estimate_cost(best.route,terrain,p,costs);const auto initial_cost=best.estimate->total;
    std::ostringstream report;report<<"Opción 2: optimización local del corredor base. Curvas y radio pendientes.\n";
    std::size_t checked=0;const auto budget=std::min<std::size_t>(1024,road.max_shortcut_checks);
    if(progress)progress(0);
    for(int pass=0;pass<3&&!stop.stop_requested()&&checked<budget;++pass){
        std::vector<double> station(best.route.size());for(std::size_t i=1;i<station.size();++i)station[i]=station[i-1]+distance(best.route[i-1],best.route[i]);
        std::vector<std::size_t> anchors{0};double spacing=std::max(5.0,station.back()/256);
        for(std::size_t i=1;i+1<station.size();++i)if(station[i]-station[anchors.back()]>=spacing)anchors.push_back(i);
        if(anchors.back()!=best.route.size()-1)anchors.push_back(best.route.size()-1);
        struct Span{std::size_t first,last;double saving;};std::vector<Span> spans;
        for(std::size_t i=0;i<anchors.size();++i)for(std::size_t j=i+1;j<anchors.size()&&j<=i+32;++j){auto a=anchors[i],b=anchors[j];double h=distance(best.route[a],best.route[b]),saving=station[b]-station[a]-h;
            if(h>1e-7&&saving>0.01&&std::abs(best.route[b].z-best.route[a].z)<=p.max_grade_percent/100*h+1e-8)spans.push_back({a,b,saving});}
        std::sort(spans.begin(),spans.end(),[](auto a,auto b){return a.saving>b.saving;});bool improved=false;
        for(auto span:spans){
            if(stop.stop_requested()||checked>=budget)break;++checked;if(progress)progress(static_cast<int>(checked*99/std::max<std::size_t>(1,budget)));
            auto a=best.route[span.first],b=best.route[span.last];auto profile=terrain.segment_profile(a,b);if(!profile||profile->size()<2||profile->size()>p.max_samples)continue;
            std::vector<Vec3> section;for(auto ground:*profile)if(section.empty()||distance(section.back(),ground)>1e-8)section.push_back(ground);
            if(section.size()<2)continue;
            std::vector<double> lo(section.size()),hi(section.size());for(std::size_t i=0;i<section.size();++i){lo[i]=section[i].z-p.max_cut_m;hi[i]=section[i].z+p.max_fill_m;}
            lo.front()=hi.front()=a.z;lo.back()=hi.back()=b.z;bool feasible=true;const double grade=p.max_grade_percent/100;
            for(std::size_t i=1;i<section.size();++i){double rise=grade*distance(section[i-1],section[i]);lo[i]=std::max(lo[i],lo[i-1]-rise);hi[i]=std::min(hi[i],hi[i-1]+rise);if(lo[i]>hi[i]+1e-8){feasible=false;break;}}
            for(std::size_t i=section.size()-1;i>0&&feasible;--i){double rise=grade*distance(section[i-1],section[i]);lo[i-1]=std::max(lo[i-1],lo[i]-rise);hi[i-1]=std::min(hi[i-1],hi[i]+rise);if(lo[i-1]>hi[i-1]+1e-8)feasible=false;}
            if(!feasible)continue;
            section.front()=a;for(std::size_t i=1;i<section.size();++i){double rise=grade*distance(section[i-1],section[i]),lower=std::max(lo[i],section[i-1].z-rise),upper=std::min(hi[i],section[i-1].z+rise);
                if(lower>upper+1e-8){feasible=false;break;}double t=distance(a,section[i])/distance(a,b);section[i].z=std::clamp(std::lerp(a.z,b.z,t),lower,std::max(lower,upper));}
            if(!feasible)continue;section.back()=b;if(road.trace)road.trace(section,false);
            auto candidate=best;candidate.route.assign(best.route.begin(),best.route.begin()+span.first);candidate.route.insert(candidate.route.end(),section.begin(),section.end());candidate.route.insert(candidate.route.end(),best.route.begin()+span.last+1,best.route.end());
            if(candidate.route.size()>resources.max_states||candidate.route.size()>p.max_samples)continue;
            try{candidate.estimate=estimate_cost(candidate.route,terrain,p,costs);}catch(const std::invalid_argument&){continue;}
            if(candidate.estimate->total>=best.estimate->total-1e-6)continue;
            best=std::move(candidate);improved=true;report<<"Mejora local validada entre vértices "<<span.first+1<<" y "<<span.last+1<<" del candidato.\n";
            if(road.trace)road.trace(best.route,true);break;
        }
        if(!improved)break;
    }
    if(stop.stop_requested()){best=original;best.estimate=estimate_cost(best.route,terrain,p,costs);report<<"Cancelado; ruta base conservada.\n";}
    else report<<"Comparaciones: "<<checked<<". Objetivo inicial "<<initial_cost<<"; final "<<best.estimate->total<<". Se conserva la base si no hay mejora.\n";
    best.status=SearchStatus::found;best.adjustment_report+=report.str();if(progress&&!stop.stop_requested())progress(100);return best;
}
}
SearchResult optimize_surface_reference(const Terrain& terrain,const SearchResult& surface,const Parameters& input,const UnitCosts& costs,
    const SearchOptions& resources,const RoadOptions& road,std::stop_token stop,ProgressCallback progress){
    auto p=input;p.mode=DesignMode::surface;
    (void)estimate_sectioned_route(surface,terrain,p,costs);
    if(surface.strategy=="surface_reference")return optimize_polyline_reference(terrain,surface,input,costs,resources,road,stop,progress);
    p.mode=DesignMode::earthwork;SearchResult best=surface;
    best.estimate=estimate_sectioned_route(best,terrain,p,costs);
    bool improved=false;
    std::ostringstream report;report<<"Opción 2: se conserva la ruta de la opción 1 y se ensayan mejoras locales de corte/relleno.\n";
    std::vector<double> station(surface.route.size());
    for(std::size_t i=1;i<station.size();++i)station[i]=station[i-1]+distance(surface.route[i-1],surface.route[i]);
    std::vector<std::size_t> anchors{0};double interval=std::max(20.0,station.back()/32);
    for(std::size_t i=1;i+1<station.size();++i)if(station[i]-station[anchors.back()]>=interval)anchors.push_back(i);
    if(anchors.back()!=surface.route.size()-1)anchors.push_back(surface.route.size()-1);
    struct Span{std::size_t first,last;double saving;};std::vector<Span> candidates;
    for(auto first:anchors)for(auto last:anchors)if(last>first){double chord=distance(surface.route[first],surface.route[last]),saving=station[last]-station[first]-chord;
        if(chord>1&&saving>1&&std::abs(surface.route[last].z-surface.route[first].z)<=p.max_grade_percent/100*(station[last]-station[first]))candidates.push_back({first,last,saving});}
    std::sort(candidates.begin(),candidates.end(),[](auto a,auto b){return a.saving>b.saving;});
    auto limits=resources;limits.max_states=std::min<std::size_t>(resources.max_states,20000);limits.max_expansions=std::min<std::size_t>(resources.max_expansions,5000);limits.max_edges=std::min<std::size_t>(resources.max_edges,80000);
    const auto count=std::min<std::size_t>(3,candidates.size());if(progress)progress(0);
    for(std::size_t i=0;i<count&&!stop.stop_requested();++i){auto span=candidates[i];
        try{auto refined=optimize_route_section(terrain,surface,span.first,span.last,p,p,costs,limits,road,stop,
                [&](int value){if(progress)progress(static_cast<int>((i*100+value)/count));});
            report<<refined.report;
            if(refined.candidate&&refined.candidate->estimate->total<best.estimate->total-1e-6){best=std::move(*refined.candidate);improved=true;break;}
        }catch(const std::invalid_argument& e){report<<"Tramo conservado: "<<e.what()<<"\n";}
    }
    if(stop.stop_requested()){best=surface;best.estimate=estimate_sectioned_route(best,terrain,p,costs);improved=false;report<<"Optimización cancelada; ruta base conservada.\n";}
    else if(!improved)report<<"No se obtuvo una mejora validada; se devuelve la misma ruta base.\n";
    best.status=SearchStatus::found;best.adjustment_report+=report.str();
    if(road.trace)road.trace(best.route,true);if(progress&&!stop.stop_requested())progress(100);return best;
}

}
