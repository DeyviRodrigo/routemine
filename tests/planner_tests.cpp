#include "routemine/core.hpp"
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
using namespace routemine;
void check(bool v){if(!v)throw std::runtime_error("assertion failed");}
void near(double a,double b,double tolerance=1e-7){check(std::abs(a-b)<=tolerance);}
template<class F>void rejects(F f){bool failed=false;try{f();}catch(const std::exception&){failed=true;}check(failed);}
Terrain ridge(double ox=0,double oy=0){return Terrain({{ox,oy,0},{ox+50,oy,3},{ox+100,oy,0},{ox,oy+30,0},{ox+50,oy+30,0},{ox+100,oy+30,0}},{{0,1,3},{1,4,3},{1,2,4},{2,5,4}});}
Parameters params(){Parameters p;p.max_grade_percent=2;p.max_cut_m=5;p.max_fill_m=0;p.road_width_m=1;return p;}
UnitCosts rates(){UnitCosts c;c.road_per_m2=10;c.soil_cut_per_m3=1;return c;}
SearchOptions lattice(){SearchOptions o;o.xy_spacing_m=10;o.z_min=0;o.z_max=0;return o;}
int main(int argc,char** argv){try{check(argc==2);std::string name=argv[1];
    if(name=="obj"){
        std::istringstream obj("# test\nv 500000 8000000 0\nv 500010 8000000 10\nv 500000 8000010 0\nf -3 -2 -1 # face\n");
        auto t=load_obj(obj);near(*t.elevation(500001,8000001),1);check(!t.elevation(500009,8000009));
    }else if(name=="obj_invalid"){
        for(const auto* text:{"v 0 0 0\nf 1 2 3\n","v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n",
            "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3 1\n","v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1/1 2/2 3/3\n","v nan 0 0\n"})
            rejects([&]{std::istringstream obj(text);load_obj(obj);});
    }else if(name=="generated_economics"){
        auto t=ridge();auto p=params();auto c=rates();auto o=lattice();
        auto cheap=plan_cpu(t,{0,0,0},{100,0,0},p,c,o);check(cheap.status==SearchStatus::found);
        c.soil_cut_per_m3=20;auto costly=plan_cpu(t,{0,0,0},{100,0,0},p,c,o);check(costly.status==SearchStatus::found);
        check(cheap.estimate->cut_m3>costly.estimate->cut_m3+1);
        check(cheap.estimate->validation.metrics.horizontal_m<costly.estimate->validation.metrics.horizontal_m);
        check(!costly.estimate->validation.all_constraints_pass());
        near(cheap.route.front().x,0);near(cheap.route.back().x,100);
        std::cout<<"cheap: cut="<<cheap.estimate->cut_m3<<" length="<<cheap.estimate->validation.metrics.horizontal_m
            <<" cost="<<cheap.estimate->total<<" costly: cut="<<costly.estimate->cut_m3<<" length="<<costly.estimate->validation.metrics.horizontal_m<<" cost="<<costly.estimate->total<<'\n';
    }else if(name=="generated_profile"){
        Terrain t({{0,0,0},{20,0,2},{0,10,0},{20,10,2}},{{0,1,2},{1,3,2}});
        auto p=params();p.max_grade_percent=10;p.max_cut_m=0;p.max_fill_m=0;
        auto o=lattice();o.z_max=2;o.z_step_m=1;
        auto r=plan_cpu(t,{0,0,0},{20,0,2},p,rates(),o);check(r.status==SearchStatus::found);
        near(r.route.back().z,2);near(r.estimate->validation.metrics.max_grade_percent,10);
        p.max_grade_percent=5;check(plan_cpu(t,{0,0,0},{20,0,2},p,rates(),o).status==SearchStatus::no_path);
    }else if(name=="no_path"){
        Terrain t({{0,0,0},{2,0,0},{0,2,0},{4,0,0},{6,0,0},{6,2,0}},{{0,1,2},{3,4,5}});
        auto o=lattice();o.xy_spacing_m=1;
        check(plan_cpu(t,{1,0,0},{5,0,0},params(),rates(),o).status==SearchStatus::no_path);
        auto p=params();p.max_cut_m=0;p.max_fill_m=0;p.mode=DesignMode::surface;
        auto flat=lattice();check(plan_cpu(ridge(),{0,0,0},{100,0,0},p,rates(),flat).status==SearchStatus::found);
    }else if(name=="search_limits"){
        auto o=lattice();o.max_states=2;rejects([&]{plan_cpu(ridge(),{0,0,0},{100,0,0},params(),rates(),o);});
        o=lattice();o.max_expansions=1;check(plan_cpu(ridge(),{0,0,0},{100,0,0},params(),rates(),o).status==SearchStatus::budget_exceeded);
        o=lattice();o.max_edges=1;check(plan_cpu(ridge(),{0,0,0},{100,0,0},params(),rates(),o).status==SearchStatus::budget_exceeded);
        auto c=rates();c.reusable_cut_fraction=0.5;rejects([&]{plan_cpu(ridge(),{0,0,0},{100,0,0},params(),c,lattice());});
        auto p=params();p.max_cut_m=2;auto r=plan_cpu(ridge(),{0,0,0},{100,0,0},p,rates(),lattice());check(r.status==SearchStatus::found);check(r.estimate->validation.max_cut_m<=2+1e-8);
    }else if(name=="discontinuity"){
        Terrain gap({{0,0,0},{1,0,0},{0,1,0},{1.001,0,0},{2,0,0},{2,1,0}},{{0,1,2},{3,4,5}});
        check(!gap.segment_profile({0.1,0,0},{1.9,0,0}));
        Parameters p;p.sample_spacing_m=100;
        check(!validate(std::vector<Vec3>{{0.1,0,0},{1.9,0,0}},gap,p).checked_constraints_pass());
        // A narrow ridge cannot be skipped by a coarse uniform sample spacing.
        auto r=validate(std::vector<Vec3>{{0,0,0},{100,0,0}},ridge(),p);near(r.max_cut_m,3);
    }else if(name=="large_coordinates"){
        auto r=plan_cpu(ridge(500000,8000000),{500000,8000000,0},{500100,8000000,0},params(),rates(),lattice());
        auto base=plan_cpu(ridge(),{0,0,0},{100,0,0},params(),rates(),lattice());
        check(r.status==SearchStatus::found);near(r.estimate->total,base.estimate->total,1e-6);
        for(auto p:r.route)check(p.x>=500000&&p.y>=8000000);
    }else throw std::runtime_error("unknown test");
    std::cout<<name<<": PASS\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
