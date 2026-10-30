#include "routemine/config.hpp"
#include <cmath>
#include <chrono>
#include <fstream>
#include <iostream>
#include <numbers>
#include <sstream>
#include <stdexcept>
using namespace routemine;
void check(bool v,const char* message="assertion failed"){if(!v)throw std::runtime_error(message);}
template<class F>void rejects(F f){bool bad=false;try{f();}catch(const std::exception&){bad=true;}check(bad);}
Terrain plane(double size=200,double slope=0.2){return Terrain({{0,0,0},{size,0,size*slope},{0,size,0},{size,size,size*slope}},{{0,1,2},{1,3,2}});}
Terrain hill(){double h=20*std::tan(15*std::numbers::pi/180);return Terrain({{0,-500,0},{20,-500,h},{40,-500,0},{0,500,0},{20,500,h},{40,500,0}},{{0,1,3},{1,4,3},{1,2,4},{2,5,4}});}
Parameters parameters(double grade=10){Parameters p;p.max_grade_percent=grade;p.max_cut_m=6;p.max_fill_m=6;p.road_width_m=8;p.min_radius_m=0;p.min_straight_m=0;return p;}
UnitCosts costs(){UnitCosts c;c.road_per_m2=1;return c;}
void output(const RoadAlternatives& r){std::cout<<"status="<<static_cast<int>(r.surface.status)<<" length="<<(r.surface.estimate?r.surface.estimate->validation.metrics.horizontal_m:0)<<" expanded="<<r.surface.expanded<<" edges="<<r.surface.edges_checked<<" spacing="<<r.surface.effective_xy_spacing_m<<" headings="<<r.surface.effective_heading_step_degrees<<'\n';}
int main(int argc,char** argv){try {
    check(argc>=2);std::string name=argv[1];
    if(name=="slope_route") {
        auto t=plane();auto p=parameters();auto r=plan_road(t,{10,100,2},{190,100,38},p,costs(),SearchOptions{});output(r);
        check(r.surface.status==SearchStatus::found,"surface route not found");check(r.surface.estimate->validation.metrics.horizontal_m>=360-1e-6);
        check(r.surface.estimate->validation.checked_constraints_pass());check(r.surface.estimate->validation.metrics.max_grade_percent<=10+1e-7);
    } else if(name=="large_domain") {
        auto t=plane(4000);auto p=parameters(12);SearchOptions o;o.max_states=500;o.xy_spacing_m=5;
        auto r=plan_road(t,{100,2000,20},{3900,2000,780},p,costs(),o);output(r);check(r.surface.status==SearchStatus::found);check(r.surface.effective_xy_spacing_m>5);
        check(r.surface.estimate->validation.metrics.max_grade_percent<=12+1e-7);
    } else if(name=="hill_intervention"||name=="strict_ratio"||name=="depth_limit"||name=="metadata") {
        auto t=hill();auto p=parameters(2);SearchOptions o;o.xy_spacing_m=5;RoadOptions settings;
        if(name=="strict_ratio")settings.detour_ratio=100;
        if(name=="depth_limit")p.max_cut_m=1;
        auto r=plan_road(t,{0,0,0},{40,0,0},p,costs(),o,settings);output(r);check(r.surface.status==SearchStatus::found,"hill surface route not found");
        if(name=="strict_ratio"||name=="depth_limit")check(!r.earthwork);
        else {
            check(r.earthwork.has_value(),"earthwork proposal not found");auto& e=*r.earthwork;check(!e.interventions.empty());
            check(e.estimate->validation.metrics.max_grade_percent<=2+1e-7);check(e.estimate->validation.metrics.horizontal_m<r.surface.estimate->validation.metrics.horizontal_m/5);
            for(auto span:e.interventions){check(span.detour_m>5*span.proposal_m);check(std::abs(span.terrain_angle_degrees-15)<1e-6);check(span.max_cut_m<=6);}
            if(name=="metadata") {
                auto tampered=e;tampered.interventions[0].reference_last=tampered.interventions[0].reference_first;rejects([&]{validate_interventions(tampered,t,p,settings);});
                tampered=e;tampered.reference_route[1].z+=20;rejects([&]{validate_interventions(tampered,t,p,settings);});
                RoadOptions threshold=settings;threshold.detour_ratio=100;rejects([&]{validate_interventions(e,t,p,threshold);});
            }
        }
    } else if(name=="angle_tolerance") {
        auto p=parameters(12);RoadOptions r;check(std::abs(road_parameters(p,r,true).max_grade_percent-12)<1e-10);
        r.additional_grade_degrees=1;auto proposed=road_parameters(p,r,true);check(proposed.max_grade_percent>13&&proposed.max_grade_percent<14);
        check(road_parameters(p,r,false).max_grade_percent==12);p.max_grade_percent=100;rejects([&]{road_parameters(p,r,true);});
    } else if(name=="cancel") {
        std::stop_source stop;stop.request_stop();auto r=plan_road(plane(),{0,0,0},{100,0,20},parameters(),costs(),SearchOptions{},RoadOptions{},stop.get_token());check(r.surface.status==SearchStatus::cancelled);
    } else if(name=="gap") {
        Terrain t({{0,0,0},{2,0,0},{0,2,0},{4,0,0},{6,0,0},{6,2,0}},{{0,1,2},{3,4,5}});
        SearchOptions o;o.xy_spacing_m=0.5;auto r=plan_road(t,{1,0,0},{5,0,0},parameters(),costs(),o);check(r.surface.status!=SearchStatus::found&&!r.earthwork);
        rejects([&]{plan_road(t,{1,0,100},{5,0,0},parameters(),costs(),o);});
    } else if(name=="crease") {
        Terrain t({{0,0,0},{100,0,50},{0,100,0},{100,100,0}},{{0,1,2},{1,3,2}});
        auto p=parameters(12);SearchOptions o;o.xy_spacing_m=1000;RoadOptions r;r.propose_earthwork=false;
        auto plan=plan_road(t,{30,50,15},{55,68,16},p,costs(),o,r);output(plan);
        check(plan.surface.status==SearchStatus::found,"Route across a crease not found");check(plan.surface.strategy=="tin_surface");
        check(plan.surface.estimate->validation.metrics.horizontal_m<60);check(plan.surface.estimate->validation.checked_constraints_pass());
    } else if(name=="coverage_rejection") {
        // A conflicting face lies off the edge midpoint. The old mesh check
        // accepted A/B and estimate_cost aborted the entire search afterward.
        Terrain t({{0,0,0},{100,0,0},{0,100,0},{100,100,0},
                   {20,9,1},{21,9,1},{20,11,1}},{{0,1,2},{1,3,2},{4,5,6}});
        const Vec3 a{10,10,0},b{90,10,0};auto p=parameters(12);p.mode=DesignMode::surface;
        check(t.elevation(50,10).has_value(),"Regression midpoint must be covered");
        check(!t.segment_profile(a,b),"Regression needs an off-midpoint ambiguous interval");
        SearchOptions o;o.xy_spacing_m=1000;o.max_expansions=1;RoadOptions road;road.propose_earthwork=false;
        road.trace=[&](std::span<const Vec3> path,bool){for(std::size_t i=1;i<path.size();++i)
            check(t.segment_profile(path[i-1],path[i]).has_value(),"Search traced an edge through incompatible faces");};
        auto result=plan_road(t,a,b,p,costs(),o,road);output(result);
        check(result.surface.status==SearchStatus::found,"A safe alternate route around ambiguity was discarded");
        check(result.surface.strategy=="tin_surface","Regression did not exercise mesh fallback");
        check(result.surface.estimate&&result.surface.estimate->validation.checked_constraints_pass());
        check(validate(result.surface.route,t,p).checked_constraints_pass());
        check(result.surface.estimate->validation.metrics.horizontal_m>80+1e-6,"Ambiguous direct edge was retained");
        check(result.surface.diagnostics.coverage>0,"Rejected ambiguous edges were not diagnosed");
        auto invalid=costs();invalid.road_per_m2=-1;rejects([&]{plan_road(t,a,b,p,invalid,o,road);});
    } else if(name=="precision") {
        std::vector<Vec3> vertices;std::vector<std::array<std::size_t,3>> faces;
        for(int y=0;y<=20;++y)for(int x=0;x<=20;++x)vertices.push_back({781600+x*1.3,8443700+y*1.3,0.026*(x+y)});
        for(std::size_t y=0;y<20;++y)for(std::size_t x=0;x<20;++x){auto i=y*21+x;faces.push_back({i,i+1,i+21});faces.push_back({i+1,i+22,i+21});}
        Terrain t(vertices,faces);auto p=parameters(12);p.mode=DesignMode::surface;
        for(int i=0;i<100;++i) {
            Vec3 a{781600.117+std::fmod(i*1.173,25.0),8443700.213+std::fmod(i*1.517,25.0),0};
            Vec3 b{781600.431+std::fmod(i*2.137+4,25.0),8443700.519+std::fmod(i*1.719+8,25.0),0};
            auto za=t.elevation(a.x,a.y),zb=t.elevation(b.x,b.y);check(za&&zb,"Missing point in contiguous planar TIN");a.z=*za;b.z=*zb;
            auto profile=t.segment_profile(a,b);check(profile.has_value(),"False gap at original face crossing in UTM coordinates");
            check(validate(std::array<Vec3,2>{a,b},t,p).checked_constraints_pass(),"Contiguous planar TIN rejected");
        }
    } else if(name=="real"||name=="realprobe") {
        check(argc==4);DxfImportReport report;auto terrain=load_terrain(std::filesystem::u8path(argv[2]),&report);Configuration config;
        if(std::string(argv[3])=="auto") {
            auto midpoint=[&](std::size_t index){auto f=terrain.faces()[index];auto a=terrain.vertices()[f[0]],b=terrain.vertices()[f[1]],c=terrain.vertices()[f[2]];return Vec3{(a.x+b.x+c.x)/3,(a.y+b.y+c.y)/3,(a.z+b.z+c.z)/3};};
            config.start=midpoint(terrain.faces().size()/4);config.finish=midpoint(3*terrain.faces().size()/4);config.parameters=parameters(12);config.costs=costs();
        } else {std::ifstream request(argv[3]);config=resolve_configuration(terrain,argv[2],read_settings(request));}
        std::cout<<"A="<<config.start.x<<','<<config.start.y<<','<<config.start.z<<" B="<<config.finish.x<<','<<config.finish.y<<','<<config.finish.z<<std::endl;
        if(name=="realprobe") {
            for(auto center:{config.start,config.finish})for(double radius:{1.0,2.5,5.0,10.0,20.0,40.0,100.0}) {
                int inside=0,covered=0,allowed=0,endpoint=0;
                for(int k=0;k<144;++k) {
                    double angle=k*2*std::numbers::pi/144;Vec3 q{center.x+radius*std::cos(angle),center.y+radius*std::sin(angle),0};auto z=terrain.elevation(q.x,q.y);if(!z)continue;
                    q.z=*z;++inside;if(std::abs(q.z-center.z)<=radius*config.parameters.max_grade_percent/100)++endpoint;
                    if(terrain.segment_profile(center,q))++covered;if(terrain.segment_profile(center,q,config.parameters.max_grade_percent))++allowed;
                }
                std::cout<<"radius="<<radius<<" inside="<<inside<<" covered="<<covered<<" endpoint_grade="<<endpoint<<" allowed="<<allowed<<'\n';
            }
            return 0;
        }
        auto began=std::chrono::steady_clock::now();auto r=plan_road(terrain,config.start,config.finish,config.parameters,config.costs,config.search,config.road_search);output(r);
        std::cout<<"elapsed_s="<<std::chrono::duration<double>(std::chrono::steady_clock::now()-began).count()<<std::endl;check(r.surface.status==SearchStatus::found);
        std::ofstream surface("out/real-surface.csv");write_route_csv(surface,r.surface.route);
        if(r.earthwork){std::ofstream alternative("out/real-intervention.csv");write_route_csv(alternative,r.earthwork->route);}
    } else throw std::runtime_error("unknown test");
    std::cout<<name<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
