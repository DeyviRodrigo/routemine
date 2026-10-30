#include "routemine/core.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
using namespace routemine;
void check(bool v,const char* message="assertion failed"){if(!v)throw std::runtime_error(message);}
Terrain flat(){return Terrain({{-500,-500,0},{500,-500,0},{-500,500,0},{500,500,0}},{{0,1,2},{1,3,2}});}
SearchResult detour(){
    SearchResult r;r.status=SearchStatus::found;r.strategy="continuous_ramp";r.effective_xy_spacing_m=10;r.effective_heading_step_degrees=7.5;r.route={{-84,0,0},{0,0,0}};
    double heading=0;for(double radius:{50.,70.,90.,110.}){auto a=r.route.back();Vec3 center{a.x-radius*std::sin(heading),a.y+radius*std::cos(heading),0};auto first=r.route.size()-1;
        for(int i=1;i<=90;++i){double h=heading+i*std::numbers::pi/180;r.route.push_back({center.x+radius*std::sin(h),center.y-radius*std::cos(h),0});}
        r.curves.push_back({first,r.route.size()-1,center,90});heading+=std::numbers::pi/2;
    }r.route.push_back({200,-80,0});r.route.push_back({300,-80,0});return r;
}
int main(int argc,char** argv){try{
    check(argc==2);std::string test=argv[1];auto t=flat();Parameters base;base.continuous_curves=true;base.min_radius_m=50;base.min_straight_m=84;base.max_grade_percent=10;base.max_cut_m=2;base.max_fill_m=2;UnitCosts costs;costs.road_per_m2=1;
    if(test=="corridor"){
        Terrain hill({{0,0,0},{50,0,3},{100,0,0},{0,30,0},{50,30,0},{100,30,0}},{{0,1,3},{1,4,3},{1,2,4},{2,5,4}});
        base.continuous_curves=false;base.min_radius_m=base.min_straight_m=0;base.max_turn_degrees=180;base.mode=DesignMode::surface;base.max_cut_m=4;base.max_fill_m=0;
        SearchResult r;r.status=SearchStatus::found;r.strategy="surface_reference";r.route={{0,0,0},{0,30,0},{100,30,0},{100,0,0}};r.estimate=estimate_cost(r.route,hill,base,costs);
        SearchOptions limits;RoadOptions road;auto out=optimize_surface_reference(hill,r,base,costs,limits,road);
        check(out.status==SearchStatus::found&&out.estimate->total<r.estimate->total-1);
        check(out.route.front().x==0&&out.route.back().x==100&&out.estimate->validation.max_cut_m>0);
        auto earth=base;earth.mode=DesignMode::earthwork;check(validate(out.route,hill,earth).checked_constraints_pass());
        check(r.route.size()==4&&r.route[1].y==30);
        base.max_cut_m=0;auto kept=optimize_surface_reference(hill,r,base,costs,limits,road);check(kept.estimate->validation.max_cut_m<1e-8);
        std::stop_source stop;stop.request_stop();kept=optimize_surface_reference(hill,r,base,costs,limits,road,stop.get_token());check(kept.route.size()==r.route.size());
    }else if(test=="baseline"){
        auto r=detour();r.estimate=estimate_sectioned_route(r,t,base,costs);
        SearchOptions limits;limits.max_states=2000;limits.max_expansions=500;limits.max_edges=10000;RoadOptions road;
        auto previous=r.route;
        auto optimized=optimize_surface_reference(t,r,base,costs,limits,road);
        check(optimized.status==SearchStatus::found&&optimized.estimate->total<=r.estimate->total+1e-6);
        check(optimized.route.front().x==r.route.front().x&&optimized.route.back().x==r.route.back().x);
        check(estimate_sectioned_route(optimized,t,base,costs).validation.checked_constraints_pass());
        check(r.route.size()==previous.size());
        std::stop_source stop;stop.request_stop();auto kept=optimize_surface_reference(t,r,base,costs,limits,road,stop.get_token());
        check(kept.status==SearchStatus::found&&kept.route.size()==r.route.size());
        for(std::size_t i=0;i<r.route.size();++i)check(kept.route[i].x==r.route[i].x&&kept.route[i].y==r.route[i].y&&kept.route[i].z==r.route[i].z);
    }else if(test=="colors"){
        std::vector<Vec3> route{{0,0,-1},{100,0,1}};auto segments=earthwork_segments(t,route,0.01);bool cut=false,fill=false,normal=false;
        for(auto s:segments){cut|=s.kind==EarthworkKind::cut;fill|=s.kind==EarthworkKind::fill;normal|=s.kind==EarthworkKind::surface;check(s.edge==0);}
        check(cut&&fill&&normal,"Earthwork sign transition not split");check(segments.front().kind==EarthworkKind::cut&&segments.back().kind==EarthworkKind::fill);
        std::ostringstream csv,dxf;write_classified_route_csv(csv,t,route);write_classified_route_dxf(dxf,t,route);
        check(csv.str().find("CORTE")!=std::string::npos&&csv.str().find("RELLENO")!=std::string::npos&&csv.str().find("SOBRE_TERRENO")!=std::string::npos);
        check(dxf.str().find("ROUTETEC_RELLENO\n70\n0\n62\n3")!=std::string::npos&&dxf.str().find("ROUTETEC_CORTE\n70\n0\n62\n1")!=std::string::npos);
    }else if(test=="scopes"){
        base.max_grade_percent=5;base.max_fill_m=20;base.min_straight_m=35;SearchResult r;r.route={{0,0,0},{40,0,2},{80,0,6},{120,0,8}};r.local_sections={{1,2,12,30,35}};
        auto e=estimate_sectioned_route(r,t,base,costs);check(e.validation.checked_constraints_pass()&&std::abs(e.validation.metrics.max_grade_percent-10)<1e-8);
        r.route[1].z=3;bool bad=false;try{estimate_sectioned_route(r,t,base,costs);}catch(const std::invalid_argument&){bad=true;}check(bad,"Local relaxation leaked to another section");
    }else if(test=="surface"||test=="earthwork"||test=="cancel"){
        base.mode=test=="earthwork"?DesignMode::earthwork:DesignMode::surface;auto r=detour();r.estimate=estimate_sectioned_route(r,t,base,costs);
        auto local=base;local.min_radius_m=30;local.min_straight_m=35;local.max_grade_percent=12;SearchOptions search;search.max_states=10000;search.max_expansions=3000;search.max_edges=50000;RoadOptions road;road.max_ramp_candidates=32;
        std::stop_source stop;if(test=="cancel")stop.request_stop();
        auto improved=optimize_route_section(t,r,1,r.route.size()-2,base,local,costs,search,road,stop.get_token());
        if(test=="cancel")check(!improved.candidate&&improved.report.find("Cancelado")!=std::string::npos);
        else{check(bool(improved.candidate),improved.report.c_str());auto& candidate=*improved.candidate;
            check(candidate.estimate->total<r.estimate->total-1,"Local objective not improved");
            check(candidate.route.front().x==r.route.front().x&&candidate.route[1].x==r.route[1].x&&candidate.route.back().x==r.route.back().x);
            check(candidate.local_sections.size()==1&&candidate.local_sections[0].first==1&&candidate.local_sections[0].last==candidate.route.size()-2);
            check(estimate_sectioned_route(candidate,t,base,costs).validation.checked_constraints_pass());
            check(candidate.estimate->validation.max_cut_m<=base.max_cut_m+1e-8&&candidate.estimate->validation.max_fill_m<=base.max_fill_m+1e-8);
            if(base.mode==DesignMode::surface)check(candidate.estimate->validation.max_cut_m<1e-8&&candidate.estimate->validation.max_fill_m<1e-8);
            std::cout<<"old="<<r.estimate->validation.metrics.horizontal_m<<" new="<<candidate.estimate->validation.metrics.horizontal_m<<'\n';
        }
    }else throw std::runtime_error("Unknown refinement test");
    std::cout<<test<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
